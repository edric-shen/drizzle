# Copyright (C) 2008-2009 Sun Microsystems, Inc. All rights reserved.
# Use is subject to license terms.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301
# USA

package GenTest::Transform::ExecuteAsSPTwice;

require Exporter;
@ISA = qw(GenTest GenTest::Transform);

use strict;
use lib 'lib';

use GenTest;
use GenTest::Transform;
use GenTest::Constants;

sub transform {
	my ($class, $original_query, $executor) = @_;

	return STATUS_WONT_HANDLE if $original_query !~ m{SELECT}io;

	return [
		"CREATE DATABASE IF NOT EXISTS sptwice_db",
		"CREATE PROCEDURE sptwice_db.stored_proc_$$ () LANGUAGE SQL $original_query",
		"CALL sptwice_db.stored_proc_$$ /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",
                "CALL sptwice_db.stored_proc_$$ /* TRANSFORM_OUTCOME_UNORDERED_MATCH */",
		"DROP PROCEDURE IF EXISTS sptwice_db.stored_proc_$$"
	];
}

1;
