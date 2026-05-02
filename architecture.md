Write the boot flow

A simple table showing the memory map, where everything goes

Show the module manager and how modules are interconnected


For function jumps

fix all BL with scanner
for BLR replace with a jump to a trampoline
in that trampoline first do a mitmask to see if the data is an index or not

if the pointer is an index jump to the indexed vtable pointer
if not make sure its inside of the kernel module space

if none of those are true panic


next for data access

Use Ng bits, for akk sections only, and switch Ng when in the trampoline and calling a function in another module


next for data return

Pick a reserved register that is not allowed to be used by programs, then make that register the ID for the module. In the trampoline check to make sure the module is the correct ID before jumping anywere, if its not then panic.



