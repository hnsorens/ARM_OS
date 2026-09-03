//! kernelTests for the userspace ELF loader. `load_run_hello` is the
//! payoff: it loads the real compiled /hello (built from userland/hello.c
//! and written into rootfs.img by build.zig), admits it to the scheduler,
//! runs it, and checks the exit code it reported from EL0 -- exercising
//! ELF parsing, user address-space construction, the drop to EL0, the
//! syscall path from EL0, and pid plumbing, end to end. The rest check
//! error paths and that load/unload leaves pmm balanced.
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;
const process_if = main.process_if;
const sched_if = main.sched_if;
const pmm_if = main.pmm_if;

// --- 1. load, run, and observe the EL0 exit code -----------------

fn testLoadRunHello() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var pid: u32 = 0;
    t.expectEqual(@src(), main.load("/hello", &pid), 0);
    t.expectNotEqual(@src(), pid, 0);

    var info: abi.ProcessInfo = .{};
    t.expectEqual(@src(), process_if.get_info(pid, &info), 0);
    t.expectTrue(@src(), info.is_user);
    t.expectNotEqual(@src(), info.address_space, 0);

    t.expectEqual(@src(), sched_if.admit(pid), 0);
    t.expectEqual(@src(), sched_if.run(), 0);

    // hello.c does: write(1,"hello from EL0\n"); exit(100 + getpid()).
    var after: abi.ProcessInfo = .{};
    t.expectEqual(@src(), process_if.get_info(pid, &after), 0);
    t.expectEqual(@src(), after.state, abi.ProcessState.zombie);
    t.expectEqual(@src(), after.exit_code, @as(i32, @intCast(100 + pid)));

    t.expectEqual(@src(), main.unload(pid), 0);
    t.expectFalse(@src(), process_if.exists(pid));
    return t.result();
}

// --- 2. a missing path -----------------------------------------

fn testLoadMissingPath() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var pid: u32 = 0;
    t.expectEqual(@src(), main.load("/does/not/exist", &pid), abi.ENOENT);
    return t.result();
}

// --- 3. a non-ELF file ---------------------------------------

fn testLoadNonElf() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    var pid: u32 = 0;
    // hello.txt is the plain-text fixture already in the rootfs.
    t.expectEqual(@src(), main.load("/hello.txt", &pid), abi.ENOEXEC);
    return t.result();
}

// --- 4. unload validation ----------------------------------

fn testUnloadUnknownPid() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.unload(9_999_999), abi.EINVAL);
    return t.result();
}

// --- 5. two instances are independent address spaces ------

fn testTwoInstancesIndependent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var a: u32 = 0;
    var b: u32 = 0;
    t.expectEqual(@src(), main.load("/hello", &a), 0);
    t.expectEqual(@src(), main.load("/hello", &b), 0);
    t.expectNotEqual(@src(), a, b);

    var ia: abi.ProcessInfo = .{};
    var ib: abi.ProcessInfo = .{};
    t.expectEqual(@src(), process_if.get_info(a, &ia), 0);
    t.expectEqual(@src(), process_if.get_info(b, &ib), 0);
    t.expectNotEqual(@src(), ia.address_space, ib.address_space);

    t.expectEqual(@src(), sched_if.admit(a), 0);
    t.expectEqual(@src(), sched_if.admit(b), 0);
    t.expectEqual(@src(), sched_if.run(), 0);

    t.expectEqual(@src(), process_if.get_info(a, &ia), 0);
    t.expectEqual(@src(), process_if.get_info(b, &ib), 0);
    t.expectEqual(@src(), ia.exit_code, @as(i32, @intCast(100 + a)));
    t.expectEqual(@src(), ib.exit_code, @as(i32, @intCast(100 + b)));

    t.expectEqual(@src(), main.unload(a), 0);
    t.expectEqual(@src(), main.unload(b), 0);
    return t.result();
}

// --- 6. load + unload leaves pmm balanced ----------------

fn testPmmBalancedAfterLoadUnload() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const before = pmm_if.get_free_memory();

    var pid: u32 = 0;
    t.expectEqual(@src(), main.load("/hello", &pid), 0);
    t.expectLessThan(@src(), pmm_if.get_free_memory(), before);
    t.expectEqual(@src(), main.unload(pid), 0);

    // unload must release every backing frame and every page-table frame
    // (mmu.free skips the shared identity-map blocks, which is correct).
    t.expectEqual(@src(), pmm_if.get_free_memory(), before);
    return t.result();
}

// --- fork + wait + process tree, end to end at EL0 --------------
//
// /forktest (userland/forktest.c): the parent forks, the child checks
// its ppid and exits 7, the parent wait4()s and validates the reaped pid
// + status, exiting 0 only on full success. Puts mmu.fork, the forked
// EL0 context, fd.fork_table, the waiter/wake path and reaping all on
// the line. (The proc module registered clone/wait4 before this runs.)

fn testForkWaitEndToEnd() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const free_before = pmm_if.get_free_memory();

    var pid: u32 = 0;
    t.expectEqual(@src(), main.load("/forktest", &pid), 0);
    t.expectEqual(@src(), sched_if.admit(pid), 0);
    t.expectEqual(@src(), sched_if.run(), 0);

    var info: abi.ProcessInfo = .{};
    t.expectEqual(@src(), process_if.get_info(pid, &info), 0);
    t.expectEqual(@src(), info.state, abi.ProcessState.zombie);
    t.expectEqual(@src(), info.exit_code, @as(i32, 0)); // forktest exits 0 only on success

    t.expectEqual(@src(), main.unload(pid), 0); // frees the parent; child was already reaped
    t.expectEqual(@src(), pmm_if.get_free_memory(), free_before);
    return t.result();
}

fn testWaiterSet() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    main.removeWaiter(5);
    main.removeWaiter(6);
    t.expectFalse(@src(), main.isWaiting(5));
    main.addWaiter(5);
    t.expectTrue(@src(), main.isWaiting(5));
    t.expectFalse(@src(), main.isWaiting(6));
    main.addWaiter(5); // idempotent
    main.removeWaiter(5);
    t.expectFalse(@src(), main.isWaiting(5));
    return t.result();
}

// --- execve replaces the image in place --------------------------
//
// /exectest (userland/exectest.c): first execve's a bad path (must fail,
// caller continues), then execve's /hello with an argv. execve keeps the
// pid, so /hello exits 100 + <this pid>. Covers buildImage reuse, the
// in-place TTBR0 swap, the rebuilt initial stack, the trap-frame rewrite
// and old-image teardown.

fn testExecveReplacesImage() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const free_before = pmm_if.get_free_memory();

    var pid: u32 = 0;
    t.expectEqual(@src(), main.load("/exectest", &pid), 0);
    t.expectEqual(@src(), sched_if.admit(pid), 0);
    t.expectEqual(@src(), sched_if.run(), 0);

    var info: abi.ProcessInfo = .{};
    t.expectEqual(@src(), process_if.get_info(pid, &info), 0);
    t.expectEqual(@src(), info.state, abi.ProcessState.zombie);
    // /hello (post-exec) exits 100 + getpid(); pid is unchanged by execve.
    t.expectEqual(@src(), info.exit_code, @as(i32, @intCast(100 + pid)));

    t.expectEqual(@src(), main.unload(pid), 0);
    t.expectEqual(@src(), pmm_if.get_free_memory(), free_before);
    return t.result();
}

// --- the initial stack carries a well-formed argv + auxv --------------
//
// /auxvtest (userland/auxvtest.c) walks its own stack: argc, argv[0],
// then the auxv after envp. It exits 44 only if AT_PAGESZ == 4096,
// AT_ENTRY / AT_PHDR / AT_RANDOM are non-zero, AT_PHENT == 56, and the
// AT_RANDOM pointer is readable -- i.e. buildUserStack laid the SysV
// vector out correctly. musl's crt0 depends on exactly this shape.

fn testAuxvPresent() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };

    var pid: u32 = 0;
    t.expectEqual(@src(), main.load("/auxvtest", &pid), 0);
    t.expectEqual(@src(), sched_if.admit(pid), 0);
    t.expectEqual(@src(), sched_if.run(), 0);

    var info: abi.ProcessInfo = .{};
    t.expectEqual(@src(), process_if.get_info(pid, &info), 0);
    t.expectEqual(@src(), info.state, abi.ProcessState.zombie);
    t.expectEqual(@src(), info.exit_code, @as(i32, 44));

    t.expectEqual(@src(), main.unload(pid), 0);
    return t.result();
}

comptime {
    abi.kernelTest("waiter_set", &testWaiterSet);
    abi.kernelTest("execve_replaces_image", &testExecveReplacesImage);
    abi.kernelTest("auxv_present", &testAuxvPresent);
    abi.kernelTest("load_run_hello", &testLoadRunHello);
    abi.kernelTest("load_missing_path", &testLoadMissingPath);
    abi.kernelTest("load_non_elf", &testLoadNonElf);
    abi.kernelTest("unload_unknown_pid", &testUnloadUnknownPid);
    abi.kernelTest("two_instances_independent", &testTwoInstancesIndependent);
    abi.kernelTest("pmm_balanced_after_load_unload", &testPmmBalancedAfterLoadUnload);
    abi.kernelTest("fork_wait_end_to_end", &testForkWaitEndToEnd);
}
