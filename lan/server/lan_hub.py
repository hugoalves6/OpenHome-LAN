"""LAN-only, bounded, atomic uploads around OpenHomeNX's compatible API."""
import argparse
import hashlib
import ipaddress
import os
from pathlib import Path
import secrets
import shutil
import tempfile
import threading
import time
from http.server import ThreadingHTTPServer
import upstream_hub as upstream
import nds_boxes
import live_saves

LOCK = threading.Lock()
MAX_UPLOAD = 32 * 1024 * 1024
LAN = ipaddress.ip_network(os.environ.get('OHNX_LAN', '192.168.1.0/24'))


class Hub(upstream.Hub):
    live_states = {}

    def _live(self):
        root = os.path.abspath(self.root)
        if root not in self.live_states:
            self.live_states[root] = live_saves.LiveSaves(root)
        return self.live_states[root]

    def _live_post(self, action, size):
        if not self._need_auth(): return
        try:
            raw = self.rfile.read(size)
            if len(raw) != size: raise ValueError("Incomplete request")
            request = upstream.json.loads(raw)
            if not isinstance(request, dict): raise ValueError("Invalid request")
            with LOCK:
                result = getattr(self._live(), action)(request)
            self._send_json(result)
        except (ValueError, TypeError, OSError) as error:
            self._send_json({"error": str(error)}, getattr(error, "status", 400))

    def setup(self):
        super().setup()
        self.connection.settimeout(30)

    def allowed(self):
        addr = ipaddress.ip_address(self.client_address[0])
        if addr in LAN or addr.is_loopback:
            return True
        self._send_json({'error': 'LAN access only'}, 403)
        return False

    def _safe(self, rel):
        root = Path(self.root).resolve()
        path = (root / rel.lstrip('/')).resolve()
        try:
            path.relative_to(root)
        except ValueError:
            return None
        if any(part.startswith('.') for part in Path(rel).parts):
            return None
        return str(path)

    def _listing(self, rel):
        listing = super()._listing(rel)
        if listing:
            listing['items'] = [item for item in listing['items']
                                if not item['name'].startswith('.') and '.bak.' not in item['name']]
            for item in listing['items']:
                if not item['isDir']:
                    item['sha'] = upstream.file_sha(os.path.join(self._safe(rel), item['name']))
        return listing

    def do_GET(self):
        if self.allowed():
            u = upstream.urllib.parse.urlparse(self.path)
            if u.path == '/ohnx/live/status':
                if not self._need_auth(): return
                with LOCK:
                    result = self._live().status()
                return self._send_json(result)
            if u.path == '/ohnx/nds/box':
                if not self._need_auth(): return
                q = upstream.urllib.parse.parse_qs(u.query)
                try:
                    path = self._nds_path(q.get('path', [''])[0])
                    box = int(q.get('box', ['0'])[0])
                    with LOCK:
                        data = nds_boxes.Save(Path(path).read_bytes()).view(box)
                    self.send_response(200)
                    self.send_header('Content-Type', 'application/octet-stream')
                    self.send_header('Content-Length', str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                except (ValueError, OSError) as e:
                    self._send_json({'error': str(e)}, 400)
                return
            super().do_GET()

    def _nds_path(self, rel):
        if not isinstance(rel, str) or not rel.startswith('roms/nds/') or '/' in rel[9:] or not rel.endswith('.sav'):
            raise ValueError('Expected an NDS save path')
        path = self._safe(rel)
        if not path or not os.path.isfile(path) or os.path.getsize(path) != 0x80000:
            raise ValueError('Expected an existing raw 512 KiB DS save')
        return path

    def _nds_swap(self, size):
        if not self._need_auth(): return
        try:
            raw = self.rfile.read(size)
            if len(raw) != size: raise ValueError('Incomplete request')
            request = upstream.json.loads(raw)
            if not isinstance(request, dict): raise ValueError('Invalid request')
            rel = request.get('path', '')
            with LOCK:
                path = self._nds_path(rel)
                if self._live().blocks_write(rel):
                    return self._send_json({'error': 'Save locked by Windows edit or pending delivery'}, 423)
                old = Path(path).read_bytes()
                previous = hashlib.sha256(old).hexdigest()
                if request.get('sha256') != previous:
                    return self._send_json({'error': 'Save changed; reopen boxes before moving'}, 409)
                data = nds_boxes.Save(old).swap(request.get('source_box'), request.get('source_slot'),
                                               request.get('target_box'), request.get('target_slot'))
                parent = os.path.dirname(path)
                if shutil.disk_usage(parent).free < len(data) + 256*1024*1024:
                    raise ValueError('Insufficient backup storage')
                backup = Path(self.root)/'.backups'/rel
                backup.parent.mkdir(parents=True, exist_ok=True)
                backup = backup.with_name(backup.name+'.box-move.'+str(time.time_ns()))
                with backup.open('xb') as stream:
                    stream.write(old); stream.flush(); os.fsync(stream.fileno())
                if backup.read_bytes() != old: raise ValueError('Backup verification failed')
                fd, temporary = tempfile.mkstemp(prefix='.box-move-', dir=parent)
                try:
                    with os.fdopen(fd, 'wb') as stream:
                        stream.write(data); stream.flush(); os.fsync(stream.fileno())
                    if Path(temporary).read_bytes() != data: raise ValueError('Write verification failed')
                    os.replace(temporary, path)
                finally:
                    if os.path.exists(temporary): os.unlink(temporary)
            self._send_json({'sha256': hashlib.sha256(data).hexdigest()})
        except (ValueError, TypeError, OSError) as e:
            self._send_json({'error': str(e)}, 400)

    def do_POST(self):
        if not self.allowed():
            return
        u = upstream.urllib.parse.urlparse(self.path)
        if u.path in ('/ohnx/login', '/api/login'):
            with LOCK:
                for token, expiry in list(upstream.TOKENS.items()):
                    if expiry < time.time():
                        upstream.TOKENS.pop(token, None)
        upload = u.path == '/ohnx/upload' or u.path.startswith('/api/resources/')
        if upload and not self._need_auth():
            return
        try:
            size = int(self.headers.get('Content-Length', '0'))
        except ValueError:
            return self._send_json({'error': 'invalid length'}, 400)
        cap = MAX_UPLOAD if upload else (750000 if u.path == '/ohnx/live/commit' else 4096)
        if size < 0 or size > cap or self.headers.get('Transfer-Encoding'):
            return self._send_json({'error': 'invalid or oversized request'}, 413)
        if u.path.startswith('/ohnx/live/'):
            action = u.path.rsplit('/', 1)[-1]
            if action not in ('pulse', 'acquire', 'renew', 'release', 'commit'):
                return self._send_json({'error': 'Unknown live action'}, 404)
            return self._live_post(action, size)
        if u.path == '/ohnx/nds/swap':
            return self._nds_swap(size)
        if not upload:
            return super().do_POST()
        q = upstream.urllib.parse.parse_qs(u.query)
        rel = q.get('path', [''])[0] if u.path == '/ohnx/upload' else upstream.urllib.parse.unquote(u.path[len('/api/resources/'):])
        dest = self._safe(rel)
        if not dest or dest == self.root or size == 0:
            return self._send_json({'error': 'invalid upload path or empty save'}, 400)
        try:
            raw = self.rfile.read(size)
            if len(raw) != size:
                return self._send_json({'error': 'incomplete upload'}, 400)
            expected = self.headers.get('X-Content-SHA256')
            if expected and hashlib.sha256(raw).hexdigest() != expected:
                return self._send_json({'error': 'checksum mismatch'}, 400)
            with LOCK:
                if self._live().blocks_write(Path(dest).relative_to(Path(self.root).resolve()).as_posix()):
                    return self._send_json({'error': 'Save locked by Windows edit or pending delivery'}, 423)
                previous = self.headers.get('X-Previous-SHA256')
                current = upstream.file_sha(dest) if os.path.isfile(dest) else 'missing'
                if previous and previous != current:
                    return self._send_json({'error': 'save changed; inspect before retrying'}, 409)
                parent = os.path.dirname(dest)
                os.makedirs(parent, exist_ok=True)
                if shutil.disk_usage(parent).free < size + 256 * 1024 * 1024:
                    return self._send_json({'error': 'insufficient free storage'}, 507)
                fd, temporary = tempfile.mkstemp(prefix='.upload-', dir=parent)
                try:
                    with os.fdopen(fd, 'wb') as stream:
                        stream.write(raw)
                        stream.flush()
                        os.fsync(stream.fileno())
                    if os.path.isfile(dest):
                        backup = os.path.join(self.root, '.backups', rel.lstrip('/'))
                        os.makedirs(os.path.dirname(backup), exist_ok=True)
                        shutil.copy2(dest, backup + '.' + str(time.time_ns()) + '.' + secrets.token_hex(4))
                    os.replace(temporary, dest)
                finally:
                    if os.path.exists(temporary):
                        os.unlink(temporary)
            return self._send_json({'ok': True, 'size': size})
        except (OSError, TimeoutError):
            return self._send_json({'error': 'upload failed; previous save retained'}, 500)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bind', default='192.168.1.125')
    parser.add_argument('--port', type=int, default=8321)
    parser.add_argument('--data', default='/srv/openhome/data')
    args = parser.parse_args()
    password = os.environ.get('OHNX_PASSWORD')
    if not password:
        parser.error('OHNX_PASSWORD must be configured')
    upstream.PASSWORD = password
    Hub.root = os.path.abspath(args.data)
    os.makedirs(Hub.root, exist_ok=True)
    ThreadingHTTPServer((args.bind, args.port), Hub).serve_forever()


if __name__ == '__main__':
    main()
