-- require r/not_as_root.require
disable_query_log;
eval select "$DRIZZLE_TEST_ROOT" as running_as_root;
enable_query_log;
