# Copyright (c) 2008, 2011 Oracle and/or its affiliates. All rights reserved.
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

SET GLOBAL OPTIMIZER_SWITCH = 'mrr=off,mrr_cost_based=off,materialization=off,semijoin=off,loosescan=off,firstmatch=off,engine_condition_pushdown=off,index_condition_pushdown=off,index_merge=off,index_merge_union=off,index_merge_sort_union=off,index_merge_intersection=off';

SET GLOBAL optimizer_join_cache_level = 0;
