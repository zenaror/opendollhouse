#!/usr/bin/env python3
"""Pure parser fixtures, not CMake/native compilation or binary identity proof."""
from pathlib import Path
import importlib.util,json,hashlib,tempfile
p=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('link_input_parser',p.parent/'tools/collect_cmake_link_inputs.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
checks={}
s='obj\\ with\\ space.o: /src/a\\ b.cpp /src/tag\\#one.hpp \\\n /src/colon\\:one.hpp /src/dollar$$.hpp\n/src/a\\ b.cpp:\n'
r=m.make_rules(s);assert r==[(['obj with space.o'],['/src/a b.cpp','/src/tag#one.hpp','/src/colon:one.hpp','/src/dollar$.hpp']),(['/src/a b.cpp'],[])];checks['make_escaped_space_hash_colon_dollar_continuation_phony']=True
for text in ['no delimiter\n','x.o: $(UNEXPANDED)\n','x.o: trailing\\']:
 try:m.make_rules(text)
 except ValueError:pass
 else:raise AssertionError(text)
checks['malformed_and_variable_depfiles_rejected']=True
try:m.parse_depfile(p/'ABSENT.o.d',p/'ABSENT.o',p)
except ValueError:pass
else:raise AssertionError('missing depfile inferred')
checks['missing_depfile_refuses_inference']=True
temporary=tempfile.TemporaryDirectory(prefix='odh-link-input-parser-');f=Path(temporary.name)
def member(name,data):
 header=(name.ljust(16)+'0'.ljust(12)+'0'.ljust(6)+'0'.ljust(6)+'100644'.ljust(8)+str(len(data)).ljust(10)+'`\n').encode();assert len(header)==60
 return header+data+(b'\n' if len(data)%2 else b'')
body=b'!<arch>\n'+member('/',b'fake-index')+member('//',b'very_long_object.cpp.o/\n')+member('short.o/',b'abc')+member('/0',b'xyzq')
(f/'normal.a').write_bytes(body);assert m.archive_members(f/'normal.a')==[('short.o',3,hashlib.sha256(b'abc').hexdigest()),('very_long_object.cpp.o',4,hashlib.sha256(b'xyzq').hexdigest())];checks['archive_tables_short_long_names_and_payload_hashes']=True
(f/'thin.a').write_bytes(b'!<thin>\n')
try:m.archive_members(f/'thin.a')
except ValueError:pass
else:raise AssertionError('thin silently inferred')
checks['thin_archive_explicitly_rejected']=True
# Default repository layout: source root contains build/native. Verify that
# the generated dependency is not mislabeled as a handwritten source.
source=f/'checkout';build=source/'build/native';directory=build/'CMakeFiles/sample.dir'
directory.mkdir(parents=True);(source/'main.cpp').write_text('// source\n')
generated=build/'generated/version.hpp';generated.parent.mkdir();generated.write_text('// generated\n')
(directory/'flags.make').write_text('CXX_FLAGS = -O0\n')
(directory/'DependInfo.cmake').write_text('# dependency metadata\n')
(directory/'build.make').write_text('CMakeFiles/sample.dir/main.cpp.o: '+str(source/'main.cpp')+'\n\tcompiler -c source\n')
(directory/'link.txt').write_text('/usr/bin/c++ CMakeFiles/sample.dir/main.cpp.o -o sample\n')
obj=directory/'main.cpp.o';obj.write_bytes(b'synthetic object input')
Path(str(obj)+'.d').write_text('CMakeFiles/sample.dir/main.cpp.o: '+str(source/'main.cpp')+' '+str(generated)+'\n')
(build/'sample').write_bytes(b'synthetic linked output')
col=m.Collector(build,source,True);data=col.one('sample');col.verify_unchanged()
assert [r['path'] for r in data['source_files']]==['main.cpp']
assert [r['path'] for r in data['generated']]==['generated/version.hpp']
checks['nested_build_generated_dependency_stays_generated']=True
# The documented sibling output is outside both protected roots.
assert not m.inside((f/'outside/linked.json').resolve(),source.resolve())
assert m.inside((source/'validation-output/linked.json').resolve(),source.resolve())
checks['documented_sibling_output_is_outside_source_and_build']=True
# A directly requested archive must be checked like a supplied archive.
(directory/'link.txt').write_text('/usr/bin/ar qc libsample.a CMakeFiles/sample.dir/main.cpp.o\n')
archive=build/'libsample.a'
for bad in [b'NOT AN ARCHIVE',b'!<arch>\n'+member('main.cpp.o/',b'wrong object bytes')]:
 archive.write_bytes(bad)
 try:m.Collector(build,source,True).one('sample')
 except ValueError:pass
 else:raise AssertionError('direct archive accepted invalid or mismatched contents')
archive.write_bytes(b'!<arch>\n'+member('main.cpp.o/',obj.read_bytes()))
col=m.Collector(build,source,True);data=col.one('sample');col.verify_unchanged()
assert len(data['archives'])==1 and data['archives'][0]['members_verified_against_actual_objects']
assert data['archives'][0]['member_count']==1
checks['direct_archive_invalid_mismatch_rejected_valid_members_verified']=True
for spelling in ['-lz','-l z']:
 (directory/'link.txt').write_text('/usr/bin/c++ CMakeFiles/sample.dir/main.cpp.o '+spelling+' -o sample\n')
 col=m.Collector(build,source,False)
 assert col.nodes['sample']['search_libraries']==['-lz']
for spelling in ['-l','-l -o sample']:
 (directory/'link.txt').write_text('/usr/bin/c++ CMakeFiles/sample.dir/main.cpp.o -o sample '+spelling+'\n')
 try:m.Collector(build,source,False)
 except ValueError:pass
 else:raise AssertionError('missing library argument accepted')
checks['joined_separated_search_library_and_missing_argument']=True
# add_subdirectory link scripts run from their own binary directory. The
# executable can supply an archive from a sibling directory and OBJECT-library
# inputs without a link.txt of their own.
(directory/'link.txt').write_text('/usr/bin/c++ CMakeFiles/sample.dir/main.cpp.o -o sample\n')
def nested_object(subdir,target):
 d=build/subdir/'CMakeFiles'/f'{target}.dir';d.mkdir(parents=True)
 rel=(d/'main.cpp.o').relative_to(build).as_posix()
 (d/'flags.make').write_text('CXX_FLAGS = -O0\n')
 (d/'DependInfo.cmake').write_text('# metadata\n')
 (d/'build.make').write_text(rel+': '+str(source/'main.cpp')+'\n\tcompiler -c source\n')
 o=d/'main.cpp.o';o.write_bytes(('object '+target).encode())
 Path(str(o)+'.d').write_text(rel+': '+str(source/'main.cpp')+'\n')
 return d,o
libdir,libobj=nested_object('engine','nested_library')
(libdir/'link.txt').write_text('/usr/bin/ar qc libnested.a CMakeFiles/nested_library.dir/main.cpp.o\n')
(build/'engine/libnested.a').write_bytes(b'!<arch>\n'+member('main.cpp.o/',libobj.read_bytes()))
objdir,extraobj=nested_object('engine/shared','nested_objects')
appdir,appobj=nested_object('engine/desktop','nested_app')
response=build/'engine/desktop/inputs.rsp'
response.write_text('CMakeFiles/nested_app.dir/main.cpp.o ../shared/CMakeFiles/nested_objects.dir/main.cpp.o ../libnested.a')
(appdir/'link.txt').write_text('/usr/bin/c++ @inputs.rsp -o nested_app\n')
(build/'engine/desktop/nested_app').write_bytes(b'linked nested fixture')
col=m.Collector(build,source,True);data=col.one('nested_app');col.verify_unchanged()
assert data['object_count']==3 and data['native_targets']==['nested_app','nested_library','nested_objects']
assert len(data['archives'])==1 and data['archives'][0]['member_count']==1
assert col.nodes['nested_app']['output']==build/'engine/desktop/nested_app'
assert response in col.nodes['nested_app']['response_files']
checks['subdirectories_sibling_archive_object_library_and_local_response_file']=True
# -MT names are relative to the root make invocation; dependency paths are
# relative to the actual compiler working directory. A root shadow must not win.
root_header=build/'generated.hpp';local_header=build/'engine/desktop/generated.hpp'
root_header.write_text('// wrong root header\n');local_header.write_text('// actual local header\n')
relative=appobj.relative_to(build).as_posix()
Path(str(appobj)+'.d').write_text(relative+': '+str(source/'main.cpp')+' generated.hpp\n')
parsed=m.parse_depfile(Path(str(appobj)+'.d'),appobj,build,build/'engine/desktop')
assert local_header in parsed and root_header not in parsed
appobj.write_bytes(appobj.read_bytes())
(build/'engine/desktop/nested_app').write_bytes(b'linked nested fixture')
col=m.Collector(build,source,True);data=col.one('nested_app');col.verify_unchanged()
assert [r['path'] for r in data['generated']]==['engine/desktop/generated.hpp']
checks['relative_dependency_uses_compiler_cwd_not_root_shadow']=True
temporary.cleanup()
print(json.dumps({'scope':'pure parser checks; no compiler or game execution','passed':True,'checks':checks},indent=2))
