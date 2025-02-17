-- SPDX-License-Identifier: MPL-2.0
--
-- This Source Code Form is subject to the terms of the Mozilla Public
-- License, v. 2.0.  If a copy of the MPL was not distributed with this
-- file, You can obtain one at http://mozilla.org/MPL/2.0/.
--
-- Copyright 2024 MonetDB Foundation;
-- Copyright August 2008 - 2023 MonetDB B.V.;
-- Copyright 1997 - July 2008 CWI.

-- make the offline tracing table available for inspection
create function sys.tracelog()
	returns table (
		ticks bigint,		-- time in microseconds
		stmt string,	-- actual statement executed
		event string
	)
	external name sql.dump_trace;

create view sys.tracelog as select * from sys.tracelog();

grant execute on function sys.tracelog to public;
grant select on sys.tracelog to public;
