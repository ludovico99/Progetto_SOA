put_data_sys_num = 156
get_data_sys_num = 174
invalidate_data_sys_num = 177
multi_ops = -1
all:
	gcc user.c -o user -lpthread

put_data:
	./user $(put_data_sys_num)

get_data:
	./user $(get_data_sys_num) 19

invalidate_data:
	./user $(invalidate_data_sys_num) 1

puts:
	./user $(put_data_sys_num)

gets:
	./user $(get_data_sys_num) 0 1 19

invalidations:
	./user $(invalidate_data_sys_num) 0 19

multi-ops:
	./user $(multi_ops) 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19