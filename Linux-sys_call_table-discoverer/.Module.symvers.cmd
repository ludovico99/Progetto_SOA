cmd_/home/ludovico99/Scrivania/Progetto_SOA/Linux-sys_call_table-discoverer/Module.symvers := sed 's/\.ko$$/\.o/' /home/ludovico99/Scrivania/Progetto_SOA/Linux-sys_call_table-discoverer/modules.order | scripts/mod/modpost -m -a  -o /home/ludovico99/Scrivania/Progetto_SOA/Linux-sys_call_table-discoverer/Module.symvers -e -i Module.symvers   -T -
