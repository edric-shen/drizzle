###################################################
#Author: Jeb
#Date: 2007-06-11
#Purpose: To replace the mysqltest.c executable 
#         wait_for_slave_to_stop function and
#         return this to the test language.
#Details:
#      1) Fill in and setup variables
#      2) loop through looking for both
#         io and sql threads to stop
#      3) If loops too long die.
####################################################
connection slave;
let $row_number= 1;
let $run= 1;
let $counter= 300;

while ($run)
{
  let $io_result= query_get_value("SHOW SLAVE STATUS", Slave_IO_Running, $row_number);
  if (`SELECT '$io_result' = 'No'`){

    let $sql_result= query_get_value("SHOW SLAVE STATUS", Slave_SQL_Running, $row_number);
    if (`SELECT '$sql_result' = 'No'`){
      let $run= 0;
    }
  }
  sleep 0.1;
  if (!$counter){
    --echo "Failed while waiting for slave to stop"
    --replace_result $MASTER_MYPORT MASTER_PORT
    --replace_column 1 # 7 # 8 # 9 # 22 # 23 # 33 #
    query_vertical SHOW SLAVE STATUS;
    exit;
  }
  dec $counter;
}


