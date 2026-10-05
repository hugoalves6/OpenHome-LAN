"""Online-only remote editing. Called under the hub's shared write mutex."""
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import tempfile
import time

import nds_boxes


class LiveError(ValueError):
    def __init__(self, message, status=409):
        super().__init__(message)
        self.status = status


class LiveSaves:
    PRESENCE_SECONDS = 25
    LEASE_SECONDS = 45

    def __init__(self, root, clock=time.monotonic):
        self.root = Path(root)
        self.clock = clock
        self.devices = {}
        self.leases = {}
        self.receipts = self.root / '.live-delivery.json'
        self.pending = json.loads(self.receipts.read_text()) if self.receipts.exists() else {}

    def path(self, rel):
        if not isinstance(rel, str) or not re.fullmatch(r'roms/nds/[A-Za-z0-9][A-Za-z0-9_. -]*\.sav', rel):
            raise LiveError('Invalid DS save path', 400)
        path = (self.root / rel).resolve()
        if not path.is_relative_to(self.root.resolve()) or not path.is_file() or path.stat().st_size != 524288:
            raise LiveError('Expected an existing 512 KiB DS save', 400)
        return path

    def persist(self):
        temporary = self.receipts.with_suffix('.new')
        with temporary.open('w') as stream:
            json.dump(self.pending, stream)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, self.receipts)

    def pulse(self, request):
        card = request.get('card_id', '')
        session = request.get('session', '')
        saves = request.get('saves', [])
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,95}', card) or not re.fullmatch(r'[A-Za-z0-9_-]{1,127}', session):
            raise LiveError('Invalid console identity', 400)
        if not isinstance(saves, list) or len(saves) > 8:
            raise LiveError('Invalid console save report', 400)
        report = {}
        for item in saves:
            rel, sha = item.get('path'), item.get('sha256', '')
            self.path(rel)
            if not re.fullmatch(r'[a-f0-9]{64}', sha):
                raise LiveError('Invalid card checksum', 400)
            report[rel] = sha
        self.devices[card] = {'session': session, 'until': self.clock() + self.PRESENCE_SECONDS, 'saves': report}
        changed = False
        for rel, sha in report.items():
            delivery = self.pending.get(rel)
            if delivery and delivery['card_id'] == card and delivery['sha256'] == sha:
                if hashlib.sha256(self.path(rel).read_bytes()).hexdigest() == sha:
                    del self.pending[rel]
                    changed = True
        if changed:
            self.persist()
        return {'ok': True, 'lease_seconds': self.PRESENCE_SECONDS}

    def owner(self, rel):
        matches = [(card, device) for card, device in self.devices.items()
                   if device['until'] > self.clock() and rel in device['saves']]
        if len(matches) != 1:
            raise LiveError('Console offline or save has multiple console owners', 423)
        return matches[0]

    def status(self):
        rows = []
        paths = set(self.pending)
        folder = self.root / "roms/nds"
        if folder.exists():
            paths.update("roms/nds/" + path.name for path in folder.glob("*.sav") if not path.name.startswith("."))
        for device in self.devices.values():
            paths.update(device['saves'])
        for rel in sorted(paths):
            try:
                path = self.path(rel)
                sha = hashlib.sha256(path.read_bytes()).hexdigest()
                card, device = self.owner(rel)
                synced = device['saves'][rel] == sha
                lease = self.leases.get(rel)
                busy = bool(lease and lease['until'] > self.clock())
                rows.append({'path': rel, 'card_id': card, 'online': True, 'synced': synced,
                             'pending': rel in self.pending, 'busy': busy, 'sha256': sha})
            except LiveError:
                rows.append({'path': rel, 'online': False, 'synced': False,
                             'pending': rel in self.pending, 'busy': False})
        return {'saves': rows}

    def acquire(self, request):
        rel = request.get('path')
        path = self.path(rel)
        card, device = self.owner(rel)
        old = path.read_bytes()
        sha = hashlib.sha256(old).hexdigest()
        if rel in self.pending or device['saves'][rel] != sha:
            raise LiveError('Waiting for console synchronization', 423)
        active = self.leases.get(rel)
        if active and active['until'] > self.clock():
            raise LiveError('Save already being edited', 423)
        nds_boxes.Save(old)  # Reject uninitialized/corrupt saves before editing.
        token = secrets.token_hex(24)
        self.leases[rel] = {'token': token, 'card_id': card, 'session': device['session'],
                            'sha256': sha, 'until': self.clock() + self.LEASE_SECONDS}
        return {'lease': token, 'sha256': sha, 'bytes': base64.b64encode(old).decode(), 'card_id': card}

    def check(self, request):
        rel = request.get('path')
        self.path(rel)
        lease = self.leases.get(rel)
        if not lease or lease['until'] <= self.clock() or request.get('lease') != lease['token']:
            raise LiveError('Editing session expired; reopen this save', 423)
        card, device = self.owner(rel)
        if card != lease['card_id'] or device['session'] != lease['session']:
            raise LiveError('Console session changed; reopen this save', 423)
        if rel in self.pending:
            raise LiveError('Waiting for edited save to reach the card', 423)
        current = hashlib.sha256(self.path(rel).read_bytes()).hexdigest()
        if current != lease['sha256'] or device['saves'][rel] != current:
            raise LiveError('Save changed outside this editor; reopen it')
        lease['until'] = self.clock() + self.LEASE_SECONDS
        return lease

    def renew(self, request):
        lease = self.check(request)
        return {'ok': True, 'sha256': lease['sha256']}

    def release(self, request):
        lease = self.leases.get(request.get('path'))
        if lease and request.get('lease') == lease['token']:
            del self.leases[request['path']]
        return {'ok': True}

    def blocks_write(self, rel):
        lease = self.leases.get(rel)
        return rel in self.pending or bool(lease and lease['until'] > self.clock())

    def commit(self, request):
        rel = request.get('path')
        lease = self.check(request)
        if request.get('sha256') != lease['sha256']:
            raise LiveError('Stale editor checksum')
        try:
            data = base64.b64decode(request.get('bytes', ''), validate=True)
        except (ValueError, TypeError):
            raise LiveError('Invalid save bytes', 400)
        if len(data) != 524288:
            raise LiveError('Expected exactly 512 KiB', 400)
        nds_boxes.Save(data)
        path = self.path(rel)
        sha = hashlib.sha256(data).hexdigest()
        if sha == lease['sha256']:
            return {'sha256': sha, 'delivered': True}
        if shutil.disk_usage(path.parent).free < len(data) + 256*1024*1024:
            raise LiveError('Insufficient backup storage', 507)
        backup = self.root / '.backups' / (rel + '.windows-edit.' + str(time.time_ns()))
        backup.parent.mkdir(parents=True, exist_ok=True)
        old = path.read_bytes()
        with backup.open('xb') as stream:
            stream.write(old); stream.flush(); os.fsync(stream.fileno())
        if backup.read_bytes() != old:
            raise LiveError('Backup verification failed', 500)
        fd, temporary = tempfile.mkstemp(prefix='.windows-edit-', dir=path.parent)
        try:
            with os.fdopen(fd, 'wb') as stream:
                stream.write(data); stream.flush(); os.fsync(stream.fileno())
            if Path(temporary).read_bytes() != data:
                raise LiveError('Write verification failed', 500)
            self.pending[rel] = {'card_id': lease['card_id'], 'sha256': sha}
            self.persist()  # Durable delivery intent before replacing hub bytes.
            os.replace(temporary, path)
        except BaseException:
            if path.read_bytes() == old:
                self.pending.pop(rel, None)
                self.persist()
            raise
        finally:
            if os.path.exists(temporary): os.unlink(temporary)
        lease['sha256'] = sha
        lease['until'] = self.clock() + 300  # Allow a slow DS transfer to finish before renewal.
        return {'sha256': sha, 'delivered': False}
