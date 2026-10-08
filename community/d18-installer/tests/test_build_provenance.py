"""Offline provenance and packager guards; all mutations stay in explicit scratch paths."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import build_provenance as p


def load_builder():
    spec=importlib.util.spec_from_file_location('builder',ROOT/'Build-Installer1.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


class ProvenanceTests(unittest.TestCase):
    def test_dirty_source_and_untracked_patch(self):
        state=p.source_state(ROOT.parents[1])
        self.assertIs(state['dirty'],True)
        self.assertIn('community/d18-installer/build_provenance.py',state['changed_files'])
        self.assertRegex(state['patch_sha256'],r'^[0-9A-F]{64}$')

    def test_no_git_information_degrades(self):
        folder=SCRATCH/'no-git';folder.mkdir(exist_ok=True)
        record=p.begin(folder,folder,env={'PATH':'','SYSTEMROOT':'missing'})
        self.assertEqual(record['source']['commit'],'unknown')
        self.assertEqual(record['source']['dirty'],'unknown')
        self.assertEqual(record['tools']['msvc']['status'],'unknown')
        binary=folder/'Fixture.dll';binary.write_bytes(b'inert')
        p.finish(record,folder/'build-provenance.json',[binary])
        self.assertEqual(p.verify(folder/'build-provenance.json',[binary])['outputs'][0]['size'],5)

    def test_missing_git_executable_degrades(self):
        with patch.object(p.subprocess,'run',side_effect=FileNotFoundError('git unavailable')):
            state=p.source_state(SCRATCH)
        self.assertEqual(state['commit'],'unknown')
        self.assertEqual(state['patch_sha256'],'unknown')

    def test_clean_snapshot_without_git_mutation(self):
        with patch.object(p,'query',return_value={'status':'known','output':'1'*40}), \
             patch.object(p.subprocess,'check_output',side_effect=[b'',b'',b'',b'fixture\n']):
            state=p.source_state(SCRATCH)
        self.assertIs(state['dirty'],False)
        self.assertEqual(state['changed_files'],[])

    def test_packager_rejects_one_changed_byte(self):
        b=SCRATCH/'packager';b.mkdir(exist_ok=True)
        for directory,names in [('core-output',['OptiScaler.dll']),('native-output',['D24Native.dll','D18RuntimeCheck.exe'])]:
            out=b/directory;out.mkdir(exist_ok=True)
            binaries=[]
            for name in names:
                path=out/name;path.write_bytes(b'inert-binary');binaries.append(path)
            p.finish({'schema':p.SCHEMA,'commands':[]},out/'build-provenance.json',binaries)
        builder=load_builder();builder.verify_inputs(b)
        binary=b/'core-output/OptiScaler.dll'
        data=bytearray(binary.read_bytes());data[3]^=1;binary.write_bytes(data)
        with self.assertRaisesRegex(RuntimeError,'Build provenance binary mismatch'):
            builder.verify_inputs(b)

    def test_directory_hash_changes(self):
        folder=SCRATCH/'dependency';folder.mkdir(exist_ok=True)
        file=folder/'header.h';file.write_bytes(b'one')
        first=p.tree_record(folder)
        file.write_bytes(b'two')
        self.assertNotEqual(first['tree_sha256'],p.tree_record(folder)['tree_sha256'])


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--scratch',required=True,type=Path)
    a=parser.parse_args();SCRATCH=a.scratch.resolve();SCRATCH.mkdir(parents=True,exist_ok=True)
    unittest.main(argv=[sys.argv[0]],verbosity=2)
