"""Synthetic end-to-end resource lookup; no commercial files are copied."""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

binary=sys.argv[1]
checks=0

def check(value):
    global checks
    checks+=1
    assert value, f"check {checks} failed"

def iff(chunks):
    signature=b"IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1"
    return signature.ljust(60,b"\0")+bytes(4)+b"".join(chunks)

def chunk(kind, ident, data):
    return kind+struct.pack(">IHH",76+len(data),ident,0)+bytes(64)+data

def locations(rows):
    return chunk(b"STR#",1,struct.pack("<hH",-3,len(rows))+b"".join(
        b"\1"+row.encode()+b"\0\0" for row in rows))

def family(ident, house, number, guids):
    return chunk(b"FAMI",ident,struct.pack("<II4siiiiiiI",0,9,b"IMAF",house,number,
        100,0,0,0,len(guids))+struct.pack("<"+"I"*len(guids),*guids))

def nbrs(guids):
    records=[]
    for index,guid in enumerate(guids):
        name=b"Sim"+bytes([48+index])
        record=struct.pack("<II",1,4)+name+b"\0\0"+struct.pack("<ii",0,1)
        record+=struct.pack("<80h",*([0]*80))
        record+=struct.pack("<hIii",11+index,guid,-1,0)
        records.append(record)
    return chunk(b"NBRS",1,struct.pack("<II4sI",0,62,b"SRBN",len(records))+
                 b"".join(records))

def character(guid):
    objd=bytearray(32)
    struct.pack_into("<I",objd,0,138)
    struct.pack_into("<H",objd,18,2)
    struct.pack_into("<I",objd,28,guid)
    return iff([chunk(b"OBJD",128,objd)])

def simi():
    payload=bytearray(12+32*2+34+6*4)
    struct.pack_into("<II",payload,0,0,0x33)
    payload[8:12]=b"IMIS"
    return chunk(b"SIMI",1,payload)

with tempfile.TemporaryDirectory(prefix="odh-session-entry-") as folder:
    root=Path(folder)
    (root/"Houses").mkdir();(root/"Characters").mkdir()
    guid_a=0x12345678;guid_b=0x87654321
    (root/"LotLocations.iff").write_bytes(iff([locations(["7,10,20"])]))
    (root/"Neighborhood.iff").write_bytes(iff([family(201,7,900,[guid_a,guid_b]),
                                                nbrs([guid_a,guid_b])]))
    (root/"Houses/House07.iff").write_bytes(iff([simi()]))
    (root/"Characters/User00009.iff").write_bytes(character(guid_b))
    (root/"Characters/User00002.iff").write_bytes(character(guid_a))
    run=subprocess.run([binary,"--user-data",str(root),"--mode","1","--house","7"],
                       capture_output=True,text=True)
    check(run.returncode==0)
    result=json.loads(run.stdout)
    check(result["evidence"]=="session_entry_resolved_only")
    check(result["family_chunk_id"]==201 and result["family_number"]==900 and
          result["member_count"]==2 and result["house_number"]==7)
    check(result["members"]==[
        {"guid":guid_a,"neighbor_id":11,"character_asset":"User00002.iff","object_definition_id":128},
        {"guid":guid_b,"neighbor_id":12,"character_asset":"User00009.iff","object_definition_id":128}])
    localized=subprocess.run([binary,"--user-data",str(root),"--mode","1","--house","7",
                              "--language","15"],capture_output=True,text=True)
    check(localized.returncode==0)
    localized_result=json.loads(localized.stdout)
    check(localized_result["family_chunk_id"]==201 and localized_result["house_number"]==7 and
          localized_result["member_count"]==2)
    bad=subprocess.run([binary,"--user-data",str(root),"--mode","6","--house","7"],
                       capture_output=True,text=True)
    check(bad.returncode==2 and not bad.stdout)
    bad=subprocess.run([binary,"--user-data",str(root),"--mode","1","--house","8"],
                       capture_output=True,text=True)
    check(bad.returncode==2 and not bad.stdout)

print(f"Session entry CLI: {checks} checks passed (synthetic only)")
