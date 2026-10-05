import base64
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import urllib.error
import urllib.request

from test_nds_boxes import make_save, ds
from live_saves import LiveSaves, LiveError
import test_nds_boxes as fixtures


class LiveEditing(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        self.rel = 'roms/nds/disposable.sav'
        self.path = self.root / self.rel
        self.path.parent.mkdir(parents=True)
        self.original = make_save()
        self.path.write_bytes(self.original)
        self.sha = hashlib.sha256(self.original).hexdigest()
        self.now = 0
        self.live = LiveSaves(self.root, lambda: self.now)

    def pulse(self, sha=None, session='boot-1', card='disposable-card'):
        return self.live.pulse({'card_id': card, 'session': session,
                               'saves': [{'path': self.rel, 'sha256': sha or self.sha}]})

    def open(self):
        self.pulse()
        opened = self.live.acquire({'path': self.rel})
        return {'path': self.rel, 'lease': opened['lease'], 'sha256': opened['sha256']}

    def test_offline_unsynced_busy_and_session_change(self):
        with self.assertRaises(LiveError): self.live.acquire({'path': self.rel})
        self.pulse('0'*64)
        with self.assertRaises(LiveError): self.live.acquire({'path': self.rel})
        request = self.open()
        with self.assertRaises(LiveError): self.live.acquire({'path': self.rel})
        self.assertTrue(self.live.blocks_write(self.rel))
        self.pulse(session='boot-2')
        with self.assertRaisesRegex(LiveError, 'session changed'): self.live.renew(request)
        self.live.release(request)
        self.assertFalse(self.live.blocks_write(self.rel))

    def test_timeout_locks_and_multiple_devices_fail_closed(self):
        request = self.open()
        self.now = 26
        with self.assertRaisesRegex(LiveError, 'offline'): self.live.commit(request)
        self.assertEqual(self.path.read_bytes(), self.original)
        self.pulse()
        self.pulse(card='second-card')
        with self.assertRaisesRegex(LiveError, 'multiple'): self.live.renew(request)

    def test_commit_backup_durable_delivery_and_ack(self):
        request = self.open()
        edited = ds.Save(self.original).swap(0, 0, 1, 2)
        request['bytes'] = base64.b64encode(edited).decode()
        result = self.live.commit(request)
        self.assertFalse(result['delivered'])
        self.assertEqual(self.path.read_bytes(), edited)
        backup = next((self.root / '.backups/roms/nds').iterdir())
        self.assertEqual(backup.read_bytes(), self.original)
        self.assertTrue(self.live.status()['saves'][0]['pending'])
        self.pulse()  # Old card bytes do not acknowledge the edit.
        self.assertIn(self.rel, self.live.pending)
        restarted = LiveSaves(self.root, lambda: self.now)
        self.assertTrue(restarted.blocks_write(self.rel))
        self.pulse(result['sha256'])
        self.assertNotIn(self.rel, self.live.pending)
        self.assertFalse(self.live.status()['saves'][0]['pending'])
        self.assertEqual(self.live.renew(request)['sha256'], result['sha256'])
        self.assertEqual(json.loads(self.live.receipts.read_text()), {})

    def test_stale_invalid_and_expired_edits_preserve_original(self):
        request = self.open()
        for change in ({'sha256': '0'*64}, {'bytes': '@@'}, {'bytes': base64.b64encode(b'x').decode()},
                       {'bytes': base64.b64encode(b'\xff'*524288).decode()}):
            with self.assertRaises(ValueError): self.live.commit({**request, **change})
            self.assertEqual(self.path.read_bytes(), self.original)
        self.now = 46
        self.pulse()
        with self.assertRaisesRegex(LiveError, 'expired'): self.live.renew(request)
        for rel in ('../outside.sav', 'roms/nds/.private.sav', 'roms/nds/a/../disposable.sav'):
            with self.assertRaises(LiveError): self.live.path(rel)


class LiveHTTP(fixtures.HTTP):
    def test_live_auth_lock_commit_and_delivery(self):
        rel = 'roms/nds/test.sav'
        with self.assertRaises(urllib.error.HTTPError) as caught:
            self.request('/ohnx/live/status', auth=False)
        self.assertEqual(caught.exception.code, 401)
        pulse = {'card_id': 'test-card', 'session': 'test-session',
                 'saves': [{'path': rel, 'sha256': hashlib.sha256(self.original).hexdigest()}]}
        self.request('/ohnx/live/pulse', pulse)
        opened = json.loads(self.request('/ohnx/live/acquire', {'path': rel}))
        self.assertEqual(base64.b64decode(opened['bytes']), self.original)
        for alias in (rel, 'roms/nds/../nds/test.sav'):
            req = urllib.request.Request(self.url + '/ohnx/upload?path=' + alias,
                                         data=self.original, headers={'X-Auth': self.token})
            with self.assertRaises(urllib.error.HTTPError) as caught: urllib.request.urlopen(req)
            self.assertIn(caught.exception.code, (400, 423))
        edited = ds.Save(self.original).swap(0, 0, 1, 2)
        result = json.loads(self.request('/ohnx/live/commit', {
            'path': rel, 'lease': opened['lease'], 'sha256': opened['sha256'],
            'bytes': base64.b64encode(edited).decode()}))
        self.assertFalse(result['delivered'])
        self.assertTrue(json.loads(self.request('/ohnx/live/status'))['saves'][0]['pending'])
        pulse['saves'][0]['sha256'] = result['sha256']
        self.request('/ohnx/live/pulse', pulse)
        self.assertFalse(json.loads(self.request('/ohnx/live/status'))['saves'][0]['pending'])


if __name__ == '__main__': unittest.main()
