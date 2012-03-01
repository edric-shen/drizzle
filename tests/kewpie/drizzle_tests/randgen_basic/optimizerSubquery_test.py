#! /usr/bin/env python
# -*- mode: python; indent-tabs-mode: nil; -*-
# vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
#
# Copyright (C) 2011 Patrick Crews
#
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

from lib.util.mysqlBaseTestCase import mysqlBaseTestCase

server_requirements = [[]]
servers = []
server_manager = None
test_executor = None

class basicTest(mysqlBaseTestCase):

    def test_optimizerSubquery1(self):
        self.servers = servers
        test_cmd = "./gentest.pl --sqltrace --gendata=conf/drizzle/drizzle.zz --grammar=conf/drizzle/optimizer_subquery_drizzle.yy --queries=1000 --threads=3"
        retcode, output = self.execute_randgen(test_cmd, test_executor, servers[0])
        self.assertEqual(retcode, 0, msg = output)
        self.assertTrue(servers[0].ping(quiet=True), msg=servers[0].dump_errlog())
        print output

    #def tearDown(self):
    #        server_manager.reset_servers(test_executor.name)
