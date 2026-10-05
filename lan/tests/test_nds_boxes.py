"""Disposable fixtures exercise block selection, preservation, and HTTP edits."""
from pathlib import Path
import hashlib
import json
import struct
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'server'))
import nds_boxes as ds
import lan_hub
import upstream_hub

def encrypt(clear):
    pattern=ds.SHUFFLE[((ds.u32(clear,0)>>13)&31)%24]
    out=bytearray(clear)
    for logical,encrypted in enumerate(pattern):
        out[8+encrypted*32:40+encrypted*32]=clear[8+logical*32:40+logical*32]
    seed=ds.u16(clear,6)
    for offset in range(8,136,2):
        seed=(seed*0x41c64e6d+0x6073)&0xffffffff
        struct.pack_into('<H',out,offset,ds.u16(out,offset)^(seed>>16))
    return bytes(out)

def fixture_mon(generation):
    name='hgss_box0_slot0.pk4' if generation==4 else 'bw_box0_slot0_tepig.pk5'
    record=(ROOT/'tests/fixtures'/name).read_bytes()
    clear=ds.decrypt(record)
    assert clear is not None
    return clear,encrypt(clear)

def make_save(generation=4,layout='HeartGold/SoulSilver'):
    data=bytearray(0x80000)
    clear,record=fixture_mon(generation)
    if generation==4:
        gsize,ssize,start,footer,base,stride=ds.LAYOUTS[layout]
        for partition in (0,0x40000):
            struct.pack_into('<I',data,partition+gsize-8,0x20060623)
            struct.pack_into('<I',data,partition+start+ssize-8,0x20060623)
            struct.pack_into('<II',data,partition+gsize-0x14,1,0)
            struct.pack_into('<II',data,partition+start+ssize-0x14,1,0)
        data[start+base:start+base+136]=record
        for partition in (0,0x40000):
            for offset,size in ((partition,gsize),(partition+start,ssize)):
                struct.pack_into('<H',data,offset+size-2,ds.crc(data[offset:offset+size-footer]))
    else:
        data[0x1941f]=22
        data[0x400:0x488]=record
        struct.pack_into('<H',data,0x194b2,ds.crc(data[0x19400:0x194b0]))
        for box in range(24):
            start=0x400+box*0x1000
            value=ds.crc(data[start:start+0xff0])
            struct.pack_into('<H',data,start+0xff2,value)
            struct.pack_into('<H',data,0x25f00+2*(box+1),value)
        struct.pack_into('<H',data,0x25fa2,ds.crc(data[0x25f00:0x25f94]))
    return bytes(data)

class Parser(unittest.TestCase):
    def test_known_pokemon_crypto_and_views(self):
        for gen in (4,5):
            clear,record=fixture_mon(gen)
            self.assertEqual(ds.decrypt(record),clear)
            view=ds.mon_view(record,gen)
            self.assertEqual(ds.u16(view,0),ds.u16(clear,8))
            self.assertEqual(view[90],1)
            self.assertGreaterEqual(view[2],1)
            if gen==5:self.assertEqual(ds.u16(view,0),498) # Upstream Tepig fixture.
        self.assertEqual(ds.mon_view(encrypt(bytes(136)),4)[90],0)

    def test_gen4_active_blocks_independent_and_validated(self):
        data=bytearray(make_save())
        gsize,ssize,start,footer,base,stride=ds.LAYOUTS['HeartGold/SoulSilver']
        struct.pack_into('<II',data,0x40000+gsize-0x14,2,0)
        struct.pack_into('<H',data,0x40000+gsize-2,ds.crc(data[0x40000:0x40000+gsize-footer]))
        save=ds.Save(data)
        self.assertEqual(save.general,0x40000)
        self.assertEqual(save.storage,start)
        data[start+ssize-2]^=1
        save=ds.Save(data)
        self.assertEqual(save.storage,0x40000+start)

    def test_rearrangement_preserves_every_other_byte(self):
        for gen in (4,5):
            layouts=ds.LAYOUTS if gen==4 else ['unused']
            for layout in layouts:
                before=make_save(gen,layout)
                save=ds.Save(before)
                after=save.swap(0,0,1,2)
                parsed=ds.Save(after)
                self.assertEqual(parsed.record(1,2),save.record(0,0))
                self.assertEqual(parsed.record(0,0),bytes(136))
                allowed=set(range(save.offset(0,0),save.offset(0,0)+136))|set(range(save.offset(1,2),save.offset(1,2)+136))
                if gen==4:
                    offset=save.storage+save.layout[1]-2
                    allowed.update((offset,offset+1))
                else:
                    for box in (0,1):
                        for offset in (0x400+box*0x1000+0xff2,0x25f00+2*(box+1)):
                            allowed.update((offset,offset+1))
                    allowed.update((0x25fa2,0x25fa3))
                    self.assertEqual(ds.u16(after,0x25fa2),ds.crc(after[0x25f00:0x25f94]))
                changed={i for i,(a,b) in enumerate(zip(before,after)) if a!=b}
                self.assertTrue(changed<=allowed)

    def test_invalid_records_empty_uninitialized_and_bounds(self):
        with self.assertRaisesRegex(ValueError,'not initialized'):ds.Save(b'\xff'*0x80000)
        save=ds.Save(make_save())
        for b,s in ((-1,0),(18,0),(0,30),(True,0)):
            with self.assertRaises(ValueError):save.offset(b,s)
        with self.assertRaisesRegex(ValueError,'empty'):save.swap(0,1,0,2)
        data=bytearray(save.data);data[save.offset(0,0)+12]^=1
        start,size,footer=save.storage,save.layout[1],save.layout[3]
        struct.pack_into('<H',data,start+size-2,ds.crc(data[start:start+size-footer]))
        with self.assertRaisesRegex(ValueError,'damaged'):ds.Save(data).swap(0,0,0,1)

class HTTP(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        lan_hub.Hub.root=self.temp.name
        self.path=Path(self.temp.name)/'roms/nds/test.sav'
        self.path.parent.mkdir(parents=True)
        self.original=make_save()
        self.path.write_bytes(self.original)
        upstream_hub.PASSWORD='test-boxes'
        self.server=ThreadingHTTPServer(('127.0.0.1',0),lan_hub.Hub)
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
        self.url='http://127.0.0.1:'+str(self.server.server_port)
        self.token=json.loads(self.request('/ohnx/login',{'password':'test-boxes'},auth=False))['token']
    def tearDown(self):
        self.server.shutdown();self.server.server_close();self.thread.join();self.temp.cleanup()
    def request(self,path,body=None,auth=True):
        req=urllib.request.Request(self.url+path,data=json.dumps(body).encode() if body is not None else None,
            headers={'X-Auth':self.token} if auth else {})
        return urllib.request.urlopen(req,timeout=5).read()
    def test_box_move_cas_backup_and_auth(self):
        view=self.request('/ohnx/nds/box?path=roms/nds/test.sav&box=0')
        self.assertEqual(len(view),ds.WIRE_SIZE)
        self.assertEqual(view[8:40],hashlib.sha256(self.original).digest())
        with self.assertRaises(urllib.error.HTTPError):self.request('/ohnx/nds/box?path=roms/nds/test.sav',auth=False)
        body={'path':'roms/nds/test.sav','sha256':hashlib.sha256(self.original).hexdigest(),
              'source_box':0,'source_slot':0,'target_box':1,'target_slot':2}
        response=json.loads(self.request('/ohnx/nds/swap',body))
        self.assertEqual(response['sha256'],hashlib.sha256(self.path.read_bytes()).hexdigest())
        backup=list((Path(self.temp.name)/'.backups').rglob('test.sav.box-move.*'))
        self.assertEqual(len(backup),1);self.assertEqual(backup[0].read_bytes(),self.original)
        with self.assertRaises(urllib.error.HTTPError) as caught:self.request('/ohnx/nds/swap',body)
        self.assertEqual(caught.exception.code,409)
        with self.assertRaises(urllib.error.HTTPError):self.request('/ohnx/nds/box?path=../escape')

if __name__=='__main__':unittest.main()
