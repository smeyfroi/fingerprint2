#!/usr/bin/env python3
"""Link a small native regression main against the host's real Release objects.

Build with make Release first. This does not replace the application executable or
its object/dependency files. Run with a logged-in macOS desktop.
"""
import argparse
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('case',choices=['context','sessions'])
    parser.add_argument('sessions',nargs='*',type=Path)
    args=parser.parse_args()
    if len(args.sessions)!=(2 if args.case=='sessions' else 0):parser.error('sessions needs two disposable session.json paths; context needs none')
    dry=subprocess.run(['make','-n','-W','src/main.cpp','Release','FP2_INNER=1'],cwd=ROOT,text=True,capture_output=True,check=True)
    lines=[shlex.split(line) for line in dry.stdout.splitlines() if line.startswith(('c++ ','clang++ '))]
    compile=next(a for a in lines if '-c' in a and 'src/main.cpp' in a)
    link=next(a for a in lines if '-c' not in a and '-o' in a and a[a.index('-o')+1]=='bin/fingerprint2')
    main_object=compile[compile.index('-o')+1]
    with tempfile.TemporaryDirectory(prefix='marksynth-studio-native-') as folder:
        folder=Path(folder)
        source=ROOT/'tests'/('imgui_context_reuse.cpp' if args.case=='context' else 'studio_session_switch.cpp')
        obj=folder/'test.o';bundle=folder/'StudioRegression.app';binary=bundle/'Contents/MacOS/StudioRegression'
        binary.parent.mkdir(parents=True)
        clean=[];i=0
        while i<len(compile):
            arg=compile[i];i+=1
            if arg in ('-MF','-MT','-o'):i+=1;continue
            if arg in ('-MMD','-MP','-DNDEBUG','-v'):continue
            clean.append(str(source) if arg=='src/main.cpp' else arg)
        subprocess.run(clean+['-o',str(obj)],cwd=ROOT,check=True)
        link=[str(obj) if a==main_object else a for a in link if a!='-v']
        link[link.index('-o')+1]=str(binary)
        subprocess.run(link,cwd=ROOT,check=True)
        framework=ROOT/'bin/fingerprint2.app/Contents/Frameworks/Syphon.framework'
        target=bundle/'Contents/Frameworks/Syphon.framework'
        shutil.copytree(framework,target,symlinks=True)
        # Standard oF data lookup from the temporary bundle; use the host fonts.
        data=ROOT/'bin/data'
        if data.exists():
            font=data/'Arial Unicode.ttf'
            if font.is_file():
                (folder/'data').mkdir();shutil.copy2(font,folder/'data'/font.name)
        subprocess.run([str(binary),*[str(p.resolve()) for p in args.sessions]],cwd=ROOT,check=True,timeout=180)

if __name__=='__main__':main()
