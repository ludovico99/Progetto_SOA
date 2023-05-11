#For kernel 5.15
put_data_sys_num = 134
get_data_sys_num = 156
invalidate_data_sys_num = 174

#For kernel 4.15
# put_data_sys_num = 134
# get_data_sys_num = 174
# invalidate_data_sys_num = 177

multi_ops = -1
same_block_ops = -2
dev_read = -3;

single-thread:
	gcc user.c -o user -lpthread 

multi-thread:
	gcc user.c -o user -lpthread -DMULTI_THREAD

dev_read: 
	./user $(dev_read)
	
puts:
	./user $(put_data_sys_num)

get_data:
	./user $(get_data_sys_num) 19

invalidate_data:
	./user $(invalidate_data_sys_num) 0

gets:
	./user $(get_data_sys_num)

invalidations:
	./user $(invalidate_data_sys_num) 0 2 4 6 8 10 12 14 16 18

#Solo multi-threads
multi-ops:
	./user $(multi_ops)

#Solo multi-threads
same-block-ops:
	./user $(same_block_ops) 0