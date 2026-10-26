"""Reproducible planning inventory; reads committed Git objects, never builds C#."""
import csv, hashlib, json, re, subprocess
from collections import Counter
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
SOURCES=[('Simitone','refs/work/Simitone-linux','e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab'),('FreeSO','refs/work/Simitone-linux/FreeSO','7e76c1960252ddd033d8937d5edb47d326f5b69c')]
# Routing families, NOT semantic reachability or proof of completed conversion.
RULES=[
 ('UI','/Simitone.Client/UI/','tools/neighborhood_screen.cpp;tools/person.cpp'),
 ('APP','/Simitone.Client/','tools/linux_game_entry.cpp'),
 ('HOST','/Simitone.Desktop/','tools/linux_window.cpp'),
 ('HOST','/Simitone.Windows/','tools/linux_window.cpp'),
 ('HOST','/Simitone.Launcher/','tools/linux_game_entry.cpp'),
 ('HOST','/Simitone.Shared/','tools/linux_game_entry.cpp'),
 ('BUILD','/tso.simantics/VMArchitecture','src/prepared_architecture.cpp'),
 ('ROUTE','/tso.simantics/Model/Routing/','src/world_attempt_walk.cpp'),
 ('SAVE','/tso.simantics/Marshals/','src/session_bundle.cpp'),
 ('SIM','/tso.simantics/Model/TS1Platform/','src/world.cpp;src/motive_change.cpp'),
 ('VM','/tso.simantics/','src/vm.cpp;src/world.cpp'),
 ('POSE','/tso.vitaboy.engine/','src/pose.cpp;tools/scene_renderer.cpp'),
 ('POSE','/tso.vitaboy.model/','src/skeleton.cpp;tools/saved_appearance.cpp'),
 ('DRAW','/tso.world/','tools/saved_lot_renderer.cpp'),
 ('AUDIO','/tso.sound/','tools/game_audio.cpp'),
 ('AUDIO','/tso.files/HIT/','src/hit_native_controller.cpp'),
 ('AUDIO','/tso.files/XA/','src/xa_audio.cpp'),
 ('AUDIO','/tso.files/UTK/','tools/game_audio.cpp'),
 ('DATA','/tso.files/','include/odh/formats.hpp;tools/resource.hpp'),
 ('PROVIDERS','/tso.content/','src/character_catalog.cpp;src/object_runtime_resources.cpp'),
 ('SHARED_UI','/tso.client/','tools/linux_text_renderer.cpp;tools/linux_vm_dialog.cpp'),
 ('HOST','/FSODroid/','tools/linux_window.cpp'),
 ('HOST','/FSO.iOS/','tools/linux_window.cpp'),
 ('SHARED_UI','/FSO.UI/','tools/linux_text_renderer.cpp'),
 ('SHARED','/tso.common/',''),
 ('SHARED','/FSO.Common.Domain/',''),
 ('JIT_REPLACE','/FSO.SimAntics.JIT','src/vm.cpp'),
 ('TOOL_REVIEW','/tso.debug/',''),
 ('TOOL_REVIEW','/Other/tools/',''),
 ('LIBRARY_REVIEW','/Other/libs/',''),
 ('PACKAGING','/FSOInstaller/',''),
 ('HOST','/FSO.Windows/','tools/linux_window.cpp'),
 ('TOOL_REVIEW','/Charvatia/',''),
 ('ASSET_TOOLS','/FSOFacadeWorker/',''),
 ('LIBRARY_REVIEW','/System.Drawing/',''),
 ('TSO_CONTENT_REVIEW','/FSO.Content.TSO/',''),
 ('HOST','/GlobalSettings.Designer.cs','tools/linux_game_entry.cpp'),
 ('SERVICE_REVIEW','/FSO.Server',''),
 ('SERVICE_REVIEW','/FSO.Common.DatabaseService/',''),
 ('TOOL_REVIEW','/FSO.IDE/',''),
 ('TOOL_REVIEW','/FSO.Patcher/',''),
]
PACKAGES={
 'APP':'UI01 QA01 PORT01','HOST':'PORT01 PORT02 PORT03 PORT04 PORT05 PORT06 PORT07 PORT08',
 'UI':'UI01 UI02 UI03 F01 LANG01 E01 E02 E03 E04 E05 E06 E07',
 'SHARED_UI':'UI01 UI03 LANG01 PORT01','BUILD':'ARCH01 ARCH02 ARCH03 B01 B02 P02',
 'ROUTE':'ROUTE01 ROUTE02','SAVE':'SAVE01 P01 P02 P03 P04 F02',
 'SIM':'SIM01 SIM02 SIM03 SIM04 SIM05 E01 E02 E03 E04 E05 E06 E07',
 'VM':'VM01 VM02 SIM01 ROUTE01','POSE':'AV01 AV02 G01',
 'DRAW':'G01 G02 AV02 PORT01','AUDIO':'AU01 AU02','DATA':'DATA01 CC01 LANG01',
 'PROVIDERS':'DATA01 B01 CC01 AV01 F01 SIM03',
 'JIT_REPLACE':'VM01 PORT01','SHARED':'DATA01 PORT01',
 'SERVICE_REVIEW':'DATA01 PORT01','TOOL_REVIEW':'DATA01 QA01',
 'LIBRARY_REVIEW':'DATA01 PORT01','PACKAGING':'PORT01 QA01',
 'ASSET_TOOLS':'G01 DATA01','TSO_CONTENT_REVIEW':'DATA01 CC01',
}
EXTENSIONS={'.cs','.csproj','.fx','.hlsl','.glsl','.props','.targets'}
def git(repo,*args):
 return subprocess.check_output(['git','-C',str(ROOT/repo),*args])
def main():
 rows=[];counts=Counter();types=0
 for name,repo,revision in SOURCES:
  entries=git(repo,'ls-tree','-rz','--full-tree',revision).split(b'\0')
  proc=subprocess.Popen(['git','-C',str(ROOT/repo),'cat-file','--batch'],stdin=subprocess.PIPE,stdout=subprocess.PIPE)
  try:
   for entry in entries:
    if not entry:continue
    meta,path=entry.split(b'\t',1);mode,kind,blob=meta.decode().split();path=path.decode()
    if kind!='blob' or Path(path).suffix.lower() not in EXTENSIONS:continue
    group,native=next(((g,n) for g,p,n in RULES if p in '/'+path),('TRIAGE',''))
    declarations=''
    if path.endswith('.cs'):
     proc.stdin.write(blob.encode()+b'\n');proc.stdin.flush()
     header=proc.stdout.readline().split();size=int(header[2]);body=proc.stdout.read(size);assert len(body)==size;assert proc.stdout.read(1)==b'\n'
     # Lexical hint only: comments/strings/partial/nested declarations require audit.
     declarations=';'.join(sorted(set(x.decode() for x in re.findall(rb'\b(?:class|struct|interface|enum|record)\s+([A-Za-z_]\w*)',body))))
     types+=bool(declarations)
    for candidate in native.split(';'):
     if candidate:assert (ROOT/candidate).is_file(),candidate
    assert group in PACKAGES,group
    rows.append([name,revision,path,blob,group,PACKAGES[group],native,declarations,'not_audited'])
    counts[(name,group)]+=1
  finally:
   proc.stdin.close();proc.stdout.close();assert proc.wait()==0
 out=ROOT/'docs/migration/source-inventory.csv'
 with out.open('w',newline='',encoding='utf8') as f:
  w=csv.writer(f);w.writerow(['repository','revision','source_path','git_blob','work_family','backlog_packages_initial','native_candidates_not_parity','declared_types_heuristic','semantic_audit']);w.writerows(rows)
 summary={'schema':1,'native_base':'6890bf141250164cc06198bdbac8f87f427c59ee','scope':'All tracked CS, shader and project-source files in both pinned repositories; not runtime reachability, methods or BHAV content coverage.','sources':[{'repository':n,'revision':v} for n,r,v in SOURCES],'rows':len(rows),'files_with_type_hints':types,'counts':[{'repository':n,'family':g,'files':v} for (n,g),v in sorted(counts.items())],'csv_sha256':hashlib.sha256(out.read_bytes()).hexdigest()}
 (out.parent/'inventory-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
 print(json.dumps({'rows':len(rows),'files_with_type_hints':types,'unrouted':sum(v for (n,g),v in counts.items() if g=='TRIAGE'),'sha256':summary['csv_sha256']}))
if __name__=='__main__':main()
