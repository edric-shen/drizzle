#! /usr/bin/env python
# -*- mode: python; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
#
# Copyright (C) 2010, 2011 Patrick Crews
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA



"""store our various default values"""

import os
import sys


def get_defaults(qp_rootdir, project_name):
    """ We store project-variable defaults here
        and return them to seed the runner

    """ 

    # Standard default values
    branch_root = os.path.dirname(os.path.dirname(qp_rootdir))
    defaults = { 'qp_root':qp_rootdir
               , 'testdir': qp_rootdir
               , 'workdir': os.path.join(qp_rootdir,'workdir')
               , 'basedir': branch_root
               , 'clientbindir': os.path.join(branch_root,'test/server/client')
               , 'server_type':'drizzle'
               , 'noshm': False
               , 'valgrind_suppression':os.path.join(qp_rootdir,'valgrind.supp')
               , 'suitepaths': [ os.path.join(branch_root,'plugin')
                           , os.path.join(branch_root,'tests/suite')
                           , os.path.join(branch_root,'tests')
                           , os.path.join(qp_rootdir,'drizzle_tests')
                           ]
               , 'suitelist' : ['randgen_main'] 
               , 'randgen_path': os.path.join(qp_rootdir,'randgen')
               , 'subunit_file': os.path.join(qp_rootdir,'workdir/test_results.subunit')
               , 'xtrabackuppath': os.path.join(branch_root,'plugin/innobase/xtrabackup/drizzlebackup.innobase')
               , 'innobackupexpath': os.path.join(branch_root,'plugin/innobase/xtrabackup/innobackupex')
               , 'tar4ibdpath': None 
               , 'wsrep_provider_path':None
               }
    return defaults

