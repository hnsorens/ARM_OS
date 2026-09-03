//! kernelTests for the signal module. The end-to-end delivery / handler /
//! rt_sigreturn path is exercised by the elf_loader `/sigtest` kernelTest
//! (a real EL0 program); here we just check the pure bookkeeping.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;

fn testRaiseForgetInherit() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    // Unknown pid / out-of-range sig are no-ops (must not crash).
    main.raise(0, 9);
    main.raise(4242, 0);
    main.raise(4242, 999);
    main.forget(4242);
    main.forkInherit(1, 2);

    // A real raise+forget round-trip: raise creates state, forget drops it.
    main.raise(7, 15); // SIGTERM pending on pid 7
    main.raise(7, 2); // SIGINT too
    main.forget(7);
    // forget again is idempotent
    main.forget(7);

    t.expectTrue(@src(), true);
    return t.result();
}

comptime {
    abi.kernelTest("raise_forget_inherit", &testRaiseForgetInherit);
}
