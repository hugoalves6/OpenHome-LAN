"""Strict Gen 4/5 box views and same-save rearrangement, derived from upstream.

Encrypted records are preserved byte for byte. Party, trainer, gifts and flags
are never rewritten by rearrangement. See nds-mini/vendor/port-assets.md.
"""
import binascii
import bisect
import hashlib
import json
from pathlib import Path
import struct

RES = json.loads(Path(__file__).with_name('nds_resources.json').read_text())
LAYOUTS = {
    'Diamond/Pearl': (0xC100, 0x121E0, 0xC100, 0x14, 4, 0xFF0),
    'Platinum': (0xCF2C, 0x121E4, 0xCF2C, 0x14, 4, 0xFF0),
    'HeartGold/SoulSilver': (0xF628, 0x12310, 0xF700, 0x10, 0, 0x1000),
}
SHUFFLE = [
    (0,1,2,3),(0,1,3,2),(0,2,1,3),(0,3,1,2),(0,2,3,1),(0,3,2,1),
    (1,0,2,3),(1,0,3,2),(2,0,1,3),(3,0,1,2),(2,0,3,1),(3,0,2,1),
    (1,2,0,3),(1,3,0,2),(2,1,0,3),(3,1,0,2),(2,3,0,1),(3,2,0,1),
    (1,2,3,0),(1,3,2,0),(2,1,3,0),(3,1,2,0),(2,3,1,0),(3,2,1,0),
]
ZERO = bytes(136)
HEADER = 128
RECORD = 256
WIRE_SIZE = HEADER + 30 * RECORD

def u16(data, offset): return struct.unpack_from('<H', data, offset)[0]
def u32(data, offset): return struct.unpack_from('<I', data, offset)[0]
def crc(data): return binascii.crc_hqx(data, 0xffff)

def decrypt(record):
    if len(record) != 136:
        raise ValueError('Wrong Pokemon record length')
    if record == ZERO:
        return None
    seed = u16(record, 6)
    shuffled = bytearray(record)
    for offset in range(8, 136, 2):
        seed = (seed * 0x41c64e6d + 0x6073) & 0xffffffff
        struct.pack_into('<H', shuffled, offset, u16(record, offset) ^ (seed >> 16))
    pattern = SHUFFLE[((u32(record, 0) >> 13) & 31) % 24]
    clear = record[:8] + b''.join(shuffled[8+block*32:40+block*32] for block in pattern)
    if sum(struct.unpack_from('<64H', clear, 8)) & 0xffff != u16(clear, 6):
        raise ValueError('Pokemon checksum mismatch')
    return clear

def name_string(data, generation):
    chars = []
    for code in struct.unpack('<' + 'H'*(len(data)//2), data):
        if code in (0, 0xffff): break
        value = int(RES['gen4_chars'].get(str(code), code)) if generation == 4 else code
        chars.append(chr(value) if value <= 0xffff else '?')
    return ''.join(chars)

def text(dest, offset, capacity, value):
    encoded = value.encode('ascii', 'replace')[:capacity-1]
    dest[offset:offset+capacity] = encoded.ljust(capacity, b'\0')

def named(table, index):
    return RES[table][index] if 0 <= index < len(RES[table]) else f'#{index}'

def mon_view(record, generation):
    out = bytearray(RECORD)
    try: clear = decrypt(record)
    except ValueError:
        out[90] = 2
        return out
    if clear is None or u16(clear,8)==0: return out
    species = u16(clear, 8)
    maximum = 493 if generation == 4 else 649
    if not 1 <= species <= maximum:
        out[90] = 2
        return out
    pid, experience = u32(clear, 0), u32(clear, 0x10)
    tid, sid, ivs = u16(clear, 0xc), u16(clear, 0xe), u32(clear, 0x38)
    growth = RES['growth_pt' if generation == 4 else 'growth_b2w2'][species]
    level = min(100, max(1, bisect.bisect_right(RES['experience'][growth], experience)))
    struct.pack_into('<H', out, 0, species)
    out[2] = level
    out[3] = pid % 25 if generation == 4 else clear[0x41]
    out[4] = ((tid ^ sid ^ (pid & 0xffff) ^ (pid >> 16)) < 8) | (((ivs >> 30) & 1) << 1)
    out[5] = (clear[0x40] >> 1) & 3
    struct.pack_into('<HH', out, 6, u16(clear, 0xa), clear[0x15])
    out[10:18] = clear[0x28:0x30]
    out[18:24] = bytes((ivs >> (i*5)) & 31 for i in range(6))
    out[24:30] = clear[0x18:0x1e]
    struct.pack_into('<IHHI', out, 30, pid, tid, sid, experience)
    text(out, 42, 24, name_string(clear[0x48:0x5e], generation))
    text(out, 66, 24, name_string(clear[0x68:0x78], generation))
    out[90] = 1
    text(out, 96, 24, named('species', species))
    for i, move in enumerate(struct.unpack_from('<4H', clear, 0x28)):
        text(out, 120+i*16, 16, named('moves', move) if move else '-')
    text(out, 184, 24, named('abilities', clear[0x15]))
    text(out, 208, 24, named('items', u16(clear, 0xa)) if u16(clear, 0xa) else 'None')
    return out

class Save:
    def __init__(self, data):
        if len(data) != 0x80000: raise ValueError('Expected a raw 512 KiB DS save')
        if data == b'\xff'*len(data) or data == bytes(len(data)):
            raise ValueError('Save not initialized: save in the game first')
        self.data = bytes(data)
        self.sha = hashlib.sha256(data).digest()
        self.layout = None
        # General and storage blocks can have DIFFERENT active partitions.
        matches = []
        for game, layout in LAYOUTS.items():
            gsize, ssize, sstart, footer, base, stride = layout
            generals = self.valid4_blocks(0, gsize, footer)
            storages = self.valid4_blocks(sstart, ssize, footer)
            if generals and storages:
                matches.append((game, layout, self.latest4(generals,gsize), self.latest4(storages,ssize)))
        if len(matches) == 1:
            self.game, self.layout, self.general, self.storage = matches[0]
            self.generation, self.boxes = 4, 18
            trainer = 0x68 if self.game == 'Platinum' else 0x64
            self.trainer = name_string(data[self.general+trainer:self.general+trainer+16],4)
            self.base, self.stride = self.layout[4:]
            return
        if matches: raise ValueError('Ambiguous DS save layout')
        game = data[0x1941f]
        if game not in (20,21,22,23): raise ValueError('Unsupported or damaged DS save')
        self.generation, self.boxes = 5, 24
        self.game = {20:'White',21:'Black',22:'White 2',23:'Black 2'}[game]
        self.trainer = name_string(data[0x19404:0x19414],5)
        self.storage, self.base, self.stride = 0, 0x400, 0x1000
        self.mirror = 0x23f00 if game in (20,21) else 0x25f00
        trainer_len = 0x68 if game in (20,21) else 0xb0
        if crc(data[0x19400:0x19400+trainer_len]) != u16(data,0x19400+trainer_len+2):
            raise ValueError('Trainer block checksum mismatch')
        for box in range(self.boxes):
            start = self.base + box*self.stride
            checksum = crc(data[start:start+0xff0])
            if checksum != u16(data,start+0xff2) or checksum != u16(data,self.mirror+2*(box+1)):
                raise ValueError(f'Box {box+1} block checksum mismatch')

    def valid4_blocks(self, start, size, footer):
        result = []
        for partition in (0,0x40000):
            offset = partition+start
            if u32(self.data,offset+size-8) not in (0x20060623,0x20070903): continue
            if crc(self.data[offset:offset+size-footer]) != u16(self.data,offset+size-2): continue
            result.append(offset)
        return result

    def latest4(self, offsets, size):
        if len(offsets)==1:return offsets[0]
        a,b=offsets
        for extra in (0,4):
            x,y=u32(self.data,a+size-0x14+extra),u32(self.data,b+size-0x14+extra)
            if x==0xffffffff and y!=0xfffffffe:return b
            if y==0xffffffff and x!=0xfffffffe:return a
            if x!=y:return a if x>y else b
        return a

    def offset(self, box, slot):
        if type(box) is not int or type(slot) is not int or not 0<=box<self.boxes or not 0<=slot<30:
            raise ValueError('Invalid box or slot')
        return self.storage+self.base+box*self.stride+slot*136

    def record(self, box, slot):
        offset=self.offset(box,slot)
        return self.data[offset:offset+136]

    def view(self, box):
        self.offset(box,0)
        out=bytearray(WIRE_SIZE)
        out[:8]=b'OHB1'+bytes((self.generation,self.boxes,box,30))
        out[8:40]=self.sha
        text(out,40,32,self.game)
        text(out,72,24,self.trainer)
        text(out,96,24,f'Box {box+1}')
        for slot in range(30):
            start=HEADER+slot*RECORD
            out[start:start+RECORD]=mon_view(self.record(box,slot),self.generation)
        return bytes(out)

    def swap(self, source_box, source_slot, target_box, target_slot):
        a,b=self.offset(source_box,source_slot),self.offset(target_box,target_slot)
        source,target=self.data[a:a+136],self.data[b:b+136]
        if mon_view(source,self.generation)[90]==0:raise ValueError('Source slot is empty')
        for record in (source,target):
            view=mon_view(record,self.generation)
            if view[90]==2:raise ValueError('Cannot rearrange a damaged Pokemon')
        data=bytearray(self.data)
        data[a:a+136],data[b:b+136]=target,source
        if self.generation==4:
            size,footer=self.layout[1],self.layout[3]
            struct.pack_into('<H',data,self.storage+size-2,crc(data[self.storage:self.storage+size-footer]))
        else:
            for box in set((source_box,target_box)):
                start=self.base+box*self.stride
                checksum=crc(data[start:start+0xff0])
                struct.pack_into('<H',data,start+0xff2,checksum)
                struct.pack_into('<H',data,self.mirror+2*(box+1),checksum)
            size=0x8c if self.mirror==0x23f00 else 0x94
            struct.pack_into('<H',data,self.mirror+size+0xe,crc(data[self.mirror:self.mirror+size]))
        result=Save(data)
        if result.record(source_box,source_slot)!=target or result.record(target_box,target_slot)!=source:
            raise ValueError('Post-edit verification failed')
        return bytes(data)


def apply_moves(data, moves):
    """Validate and apply a complete draft in memory before any file is replaced."""
    if not isinstance(moves, list) or not 1 <= len(moves) <= 128:
        raise ValueError('Expected 1 to 128 pending moves')
    for move in moves:
        if not isinstance(move, list) or len(move) != 4 or any(type(n) is not int for n in move):
            raise ValueError('Invalid pending move')
        data = Save(data).swap(*move)
    return data
