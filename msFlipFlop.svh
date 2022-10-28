/*
 * Copyright (C) 2022 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License, as published
 * by the Free Software Foundation; either version 3 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

`ifndef MSFLIPFLOP_SVH_  // guard
	`define MSFLIPFLOP_SVH_

`define MSFF_NAME msff_inst`__LINE__
`define MSFF(OUT, IN, CLK) msFlipFlop #(.WIDTH($bits(OUT))) `MSFF_NAME(CLK, IN, OUT)

`endif // guard MSFLIPFLOP_SVH_