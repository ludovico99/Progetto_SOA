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

all:
	gcc user.c -o user -lpthread

put_data:
	./user $(put_data_sys_num)

get_data:
	./user $(get_data_sys_num) 19

invalidate_data:
	./user $(invalidate_data_sys_num) 0

puts:
	./user $(put_data_sys_num)

gets:
	./user $(get_data_sys_num)

invalidations:
	./user $(invalidate_data_sys_num) 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19

multi-ops:
	./user $(multi_ops) 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19

same-block-ops:
	./user $(same_block_ops) 8	