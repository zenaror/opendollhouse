#!/usr/bin/env python3
"""Read-only CMake Unix Makefiles link-input identity. Default is PLAN, not binary binding.

Does not invoke compiler, linker, ar, app, shell, Git or network. Collection must
run after a closed build. Archive members are verified against actual .o bytes;
without a linker map they are archive INPUTS, not a claim about extracted members.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import shlex
import stat

MAX_META=32*1024*1024

def need(ok,msg):
    if not ok:raise ValueError(msg)
def inside(path,root):return path==root or root in path.parents
def path_at(text,build):
    p=Path(text);return (p if p.is_absolute() else build/p).resolve()
def read_text(path):
    need(path.is_file() and path.stat().st_size<=MAX_META,'missing/oversized metadata: '+str(path))
    return path.read_text()
def digest(path):
    need(path.is_file(),'missing input: '+str(path));h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def make_rules(text):
    """GCC Make depfile grammar: escaped whitespace/#/:/backslash, $$, continuations."""
    text=re.sub(r'\\\r?\n',' ',text)
    rules=[];left=[];right=[];token=[];rhs=False;i=0
    def flush():
        if token:(right if rhs else left).append(''.join(token));token.clear()
    while i<len(text):
        c=text[i]
        if c=='\\':
            need(i+1<len(text),'trailing depfile escape');token.append(text[i+1]);i+=2;continue
        if c=='$':
            need(i+1<len(text) and text[i+1]=='$','unexpanded Make variable in depfile');token.append('$');i+=2;continue
        if c=='#':
            j=text.find('\n',i);i=len(text) if j<0 else j;continue
        if c==':' and not rhs:flush();rhs=True;i+=1;continue
        if c in ' \t\r':flush();i+=1;continue
        if c=='\n':
            flush()
            if left or right:need(rhs and left,'invalid depfile rule');rules.append((left,right))
            left=[];right=[];rhs=False;i+=1;continue
        token.append(c);i+=1
    flush()
    if left or right:need(rhs and left,'invalid final depfile rule');rules.append((left,right))
    return rules

def parse_depfile(path,obj,build):
    rules=make_rules(read_text(path));need(rules and rules[0][1],'missing dependency rule: '+str(path))
    targets,dependencies=rules[0]
    need(obj in [path_at(x,build) for x in targets],'depfile target does not identify actual linked object: '+str(obj))
    for targets2,deps2 in rules[1:]:
        need(not deps2 and all(x in dependencies for x in targets2),'unsupported secondary depfile rule')
    return list(dict.fromkeys(path_at(x,build) for x in dependencies))

def archive_members(path):
    """Data parser only. Thin archives rejected; symbol/name tables excluded."""
    data=path.read_bytes();need(data.startswith(b'!<arch>\n'),'not a normal archive (thin unsupported): '+str(path))
    pos=8;longnames=b'';members=[]
    while pos<len(data):
        need(pos+60<=len(data),'truncated ar header');h=data[pos:pos+60];pos+=60
        need(h[58:60]==b'`\n','invalid ar terminator');size=int(h[48:58].strip());need(size>=0 and pos+size<=len(data),'truncated ar member')
        payload=data[pos:pos+size];pos+=size+(size&1);raw=h[:16].decode('ascii').strip()
        if raw=='//':longnames=payload;continue
        if raw in ('/','/SYM64/','__.SYMDEF','__.SYMDEF SORTED'):continue
        if raw.startswith('#1/'):
            n=int(raw[3:]);need(n<=len(payload),'truncated BSD ar name');name=payload[:n].rstrip(b'\0').decode();payload=payload[n:]
        elif raw.startswith('/') and raw[1:].isdigit():
            off=int(raw[1:]);need(off<len(longnames),'ar long name offset missing');end=longnames.find(b'/\n',off);need(end>=off,'ar long name unterminated');name=longnames[off:end].decode()
        else:name=raw[:-1] if raw.endswith('/') else raw
        need(name and '/' not in name and '\\' not in name and name.endswith(('.o','.obj')),'unsupported non-object archive member: '+name)
        members.append((name,len(payload),hashlib.sha256(payload).hexdigest()))
    need(pos==len(data),'ar archive alignment/extent mismatch')
    return members

class Collector:
    def __init__(self,build,source,collect):
        self.build=build.resolve();self.source=source.resolve();self.collect=collect
        need(self.build.is_dir() and self.source.is_dir() and self.build!=self.source,'invalid build/source roots')
        self.nodes={};self.outputs={};self.tracked={};self.responses={}
        for file in sorted((self.build/'CMakeFiles').glob('*.dir/link.txt')):
            name=file.parent.name[:-4];node=self.parse_link(name,file)
            need(name not in self.nodes,'duplicate target');self.nodes[name]=node
            need(node['output'] not in self.outputs,'duplicate local output producer: '+str(node['output']));self.outputs[node['output']]=name
    def pin(self,p):
        p=Path(p);need(p.is_file(),'required actual input missing: '+str(p))
        if p not in self.tracked:self.tracked[p]={'sha256':digest(p),'bytes':p.stat().st_size,'mtime_ns':p.stat().st_mtime_ns}
        return self.tracked[p]
    def expand(self,tokens,depth=0,seen=None):
        need(depth<=8,'response nesting limit');out=[]
        for token in tokens:
            if token.startswith('@'):
                p=path_at(token[1:],self.build);need(inside(p,self.build),'response outside build');self.responses[p]=read_text(p)
                if seen is not None:seen.add(p)
                out.extend(self.expand(shlex.split(self.responses[p]),depth+1,seen))
            else:out.append(token)
        need(len(out)<=200000,'link token budget');return out
    def parse_link(self,name,file):
        responses=set();commands=[self.expand(shlex.split(line),seen=responses) for line in read_text(file).splitlines() if line.strip()]
        need(commands,'empty link metadata: '+name);primary=commands[0]
        need(not any(x in primary for x in ('&&',';','|','>','<')),'shell wrapper in link metadata unsupported')
        if '-o' in primary:
            at=primary.index('-o');need(at+1<len(primary),'missing link output');output=path_at(primary[at+1],self.build);kind='linked'
        else:
            need(Path(primary[0]).name in ('ar','gcc-ar','llvm-ar') or Path(primary[0]).name.endswith('-ar'),'unsupported archive creator')
            candidates=[x for x in primary[1:] if x.endswith('.a')];need(len(candidates)==1,'archive output ambiguous');output=path_at(candidates[0],self.build);kind='archive'
        objects=[path_at(x,self.build) for x in primary[1:] if x.endswith(('.o','.obj'))]
        archives=[path_at(x,self.build) for x in primary[1:] if x.endswith('.a') and path_at(x,self.build)!=output]
        shared=[path_at(x,self.build) for x in primary[1:] if re.search(r'\.so(?:\.[0-9.]+)?$',x) and path_at(x,self.build)!=output]
        need(inside(output,self.build),'target output outside actual build')
        search_libraries=[]
        for i,token in enumerate(primary):
            if token=='-l':
                need(i+1<len(primary) and not primary[i+1].startswith('-'),'missing separated -l library argument')
                search_libraries.append('-l'+primary[i+1])
            elif token.startswith('-l'):search_libraries.append(token)
        return {'name':name,'directory':file.parent,'file':file,'output':output,'kind':kind,'objects':objects,'libraries':archives+shared,'commands':commands,
            'search_libraries':search_libraries,'response_files':sorted(responses)}
    def object_target(self,obj):
        need(inside(obj,self.build),'object outside build: '+str(obj));m=re.match(r'CMakeFiles/([^/]+)\.dir/',obj.relative_to(self.build).as_posix())
        need(m,'cannot bind object to its actual CMake target: '+str(obj));return m.group(1)
    def closure(self,target):
        need(target in self.nodes,'unknown actual linked target '+target);targets=set();objects=set();libraries=set();external=set();todo=[target];processed=set()
        while todo:
            name=todo.pop()
            if name in processed:continue
            processed.add(name);targets.add(name);node=self.nodes[name]
            for obj in node['objects']:
                objects.add(obj);owner=self.object_target(obj)
                if owner!=name:
                    # OBJECT libraries can have flags/dependencies without link.txt.
                    need((self.build/f'CMakeFiles/{owner}.dir/flags.make').is_file(),'object target flags missing');targets.add(owner)
            for lib in node['libraries']:
                if inside(lib,self.build):
                    need(lib in self.outputs,'local library has no actual CMake link metadata: '+str(lib));libraries.add(lib);todo.append(self.outputs[lib])
                elif not inside(lib,Path('/usr')) and not inside(lib,Path('/lib')):external.add(lib)
        return targets,objects,libraries,external
    def metadata(self,targets):
        result={}
        for name in sorted(targets):
            directory=self.build/f'CMakeFiles/{name}.dir'
            for suffix in ['flags.make','DependInfo.cmake','build.make']:
                p=directory/suffix;need(p.is_file(),'target metadata missing: '+str(p));result[p]=None
            if name in self.nodes:
                result[self.nodes[name]['file']]=None
                for p in self.nodes[name]['response_files']:result[p]=None
        return result
    def rows(self,paths,relative=None):
        return [dict(path=str(p.relative_to(relative)) if relative else str(p),sha256=self.pin(p)['sha256'],bytes=self.pin(p)['bytes']) for p in sorted(paths)]
    def one(self,target):
        targets,objects,libraries,external=self.closure(target);meta=self.metadata(targets);top=self.nodes[target]
        if not self.collect:
            return {'target':target,'link_output':str(top['output']),'native_targets':sorted(targets),'object_input_paths':[str(x.relative_to(self.build)) for x in sorted(objects)],'object_input_count':len(objects),
                'local_library_paths':[str(x.relative_to(self.build)) for x in sorted(libraries)],'unexpected_explicit_libraries':[str(x) for x in sorted(external)],
                'build_metadata_paths':[str(x.relative_to(self.build)) for x in sorted(meta)],'no_binary_source_binding':True}
        binary=self.pin(top['output']);sources=set();generated=set();system=set();unexpected=set(external);object_rows=[];archive_rows=[]
        for name in targets:
            node=self.nodes.get(name)
            if node:
                self.pin(node['output']);need(node['file'].stat().st_mtime_ns<=node['output'].stat().st_mtime_ns,'link metadata newer than output; build not closed: '+name)
                for lib in node['libraries']:
                    self.pin(lib);need(lib.stat().st_mtime_ns<=node['output'].stat().st_mtime_ns,'library newer than consuming link output: '+name)
        for obj in sorted(objects):
            owner=self.object_target(obj);op=self.pin(obj);dep=Path(str(obj)+'.d');need(dep.is_file(),'missing REAL .o.d, refusing inference: '+str(dep));self.pin(dep);meta[dep]=None
            ds=parse_depfile(dep,obj,self.build);need(ds,'object dependency set empty');need(any(inside(x,self.source) or inside(x,self.build) for x in ds),'object has no native/generated source authority')
            flags=self.build/f'CMakeFiles/{owner}.dir/flags.make';self.pin(flags)
            need(flags.stat().st_mtime_ns<=obj.stat().st_mtime_ns,'flags newer than actual object: '+str(obj))
            for d in ds:
                self.pin(d);need(d.stat().st_mtime_ns<=obj.stat().st_mtime_ns,'dependency newer than object; rebuild required: '+str(d))
                # A normal out-of-source build can still be nested in the
                # checkout (build/native). Its generated files remain generated.
                if inside(d,self.build):generated.add(d)
                elif inside(d,self.source):sources.add(d)
                elif inside(d,Path('/usr')) or inside(d,Path('/lib')):system.add(d)
                else:unexpected.add(d)
            need(obj.stat().st_mtime_ns<=top['output'].stat().st_mtime_ns,'object newer than binary')
            build_make=self.build/f'CMakeFiles/{owner}.dir/build.make';self.pin(build_make)
            recipe=[];active=False;relative=obj.relative_to(self.build).as_posix()
            for line in read_text(build_make).splitlines():
                if line.startswith(relative+':'):active=True;continue
                if line and not line.startswith(('\t','#')):active=False
                if active and line.startswith('\t'):recipe.append(line)
            need(recipe,'actual object compile recipe missing: '+relative)
            object_rows.append({'path':relative,'target':owner,'sha256':op['sha256'],'bytes':op['bytes'],'depfile':str(dep.relative_to(self.build)),'dependencies':[str(x) for x in ds],'compile_recipe_lines':recipe,'build_make_newer_than_object':build_make.stat().st_mtime_ns>obj.stat().st_mtime_ns})
        archive_inputs=libraries|({top['output']} if top['kind']=='archive' else set())
        for lib in sorted(archive_inputs):
            node=self.nodes[self.outputs[lib]]
            if node['kind']=='archive':
                expected=Counter((x.name,self.pin(x)['bytes'],self.pin(x)['sha256']) for x in node['objects'])
                actual=Counter(archive_members(lib));need(actual==expected,'REAL archive members differ from CMake object bytes: '+str(lib))
                need(all(x.stat().st_mtime_ns<=lib.stat().st_mtime_ns for x in node['objects']),'archive older than its actual objects')
                archive_rows.append({'path':str(lib.relative_to(self.build)),'sha256':self.pin(lib)['sha256'],'member_count':sum(actual.values()),'members_verified_against_actual_objects':True})
        target_metadata={};tools=set()
        for name in sorted(targets):
            directory=self.build/f'CMakeFiles/{name}.dir';flags=read_text(directory/'flags.make');assignments={m.group(1):m.group(2) for m in re.finditer(r'^([A-Z][A-Z0-9_]*)[ \t]*=[ \t]*(.*)$',flags,re.M)}
            target_metadata[name]={'build_make_sha256':self.pin(directory/'build.make')['sha256'],'flags_make_assignments':assignments,'link_commands':self.nodes[name]['commands'] if name in self.nodes else None,'kind':self.nodes[name]['kind'] if name in self.nodes else 'object-library'}
            if name in self.nodes:
                for command in self.nodes[name]['commands']:
                    tool=Path(command[0])
                    if tool.is_absolute() and tool.is_file():tools.add(tool.resolve())
        for p in meta:self.pin(p)
        for p in tools:self.pin(p)
        return {'sha256':binary['sha256'],'bytes':binary['bytes'],'native_targets':sorted(targets),'object_count':len(objects),'source_files':self.rows(sources,self.source),'generated':self.rows(generated,self.build),
            'build_metadata':self.rows(meta,self.build),'unexpected_external_dependencies':self.rows(unexpected),'system_dependencies':self.rows(system),'objects':object_rows,'archives':archive_rows,
            'target_metadata':target_metadata,'build_tool_files':self.rows(tools),'linker_search_dependencies':sorted({x for n in targets if n in self.nodes for x in self.nodes[n]['search_libraries']}),
            'object_count_scope':'deduplicated direct .o plus actual members of supplied local archives/shared targets; extraction/section GC not inferred without linker map'}
    def verify_unchanged(self):
        after={}
        for p,before in self.tracked.items():
            need(p.is_file(),'input disappeared during collection: '+str(p))
            row={'sha256':digest(p),'bytes':p.stat().st_size,'mtime_ns':p.stat().st_mtime_ns}
            need(row==before,'input changed during collection: '+str(p));after[str(p)]=row
        return after

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--build',required=True,type=Path);ap.add_argument('--source',required=True,type=Path);ap.add_argument('--target',action='append',required=True);ap.add_argument('--collect',action='store_true');ap.add_argument('--output',type=Path)
    args=ap.parse_args();collector=Collector(args.build,args.source,args.collect);data={n:collector.one(n) for n in args.target}
    result={'scope':'actual CMake Unix Makefiles link-input inventory','mode':'collected' if args.collect else 'planning_only','source_root':str(collector.source),'build_root':str(collector.build),
        'limits':['No compiler/linker/app/payload is executed.','Depfiles are required per real object; missing depfile fails.','Without linker map, archive members are supplied inputs, not proven extracted machine code.','Hashes and nonstale mtimes bind recorded inputs, not a reproducible-compilation attestation or runtime acceptance.','Bare -l inputs remain explicit linker-search dependencies; implicit toolchain/default search closure is not claimed.','build.make can be regenerated for unrelated dependency/link changes; current recipe lines are recorded, not asserted to be a past compiler invocation.']}
    if args.collect:
        after=collector.verify_unchanged();result.update(binaries=data,all_recorded_inputs_unchanged=True,recorded_input_count=len(collector.tracked),
            input_fingerprints_before={str(p):row for p,row in collector.tracked.items()},input_fingerprints_after=after,collector_sha256=digest(Path(__file__)))
    else:result['planning_targets']=data
    text=json.dumps(result,indent=2)+'\n'
    if args.output:
        need(not inside(args.output.resolve(),collector.source) and not inside(args.output.resolve(),collector.build),'report must be outside protected source/build')
        need(not args.output.exists() and not args.output.is_symlink(),'refuse overwrite output');args.output.parent.mkdir(parents=False,exist_ok=True)
        with args.output.open('x') as f:f.write(text)
    else:print(text,end='')
if __name__=='__main__':main()
