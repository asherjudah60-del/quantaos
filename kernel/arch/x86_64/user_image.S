.section .rodata.user_image, "a"
.global __user_client_start, __user_client_end, __user_server_start, __user_server_end
.global __user_fault_start, __user_fault_end
__user_client_start: .incbin "build/userspace/client.bin"
__user_client_end:
__user_server_start: .incbin "build/userspace/server.bin"
__user_server_end:
__user_fault_start: .incbin "build/userspace/fault.bin"
__user_fault_end:
