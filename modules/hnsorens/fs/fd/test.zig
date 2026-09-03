//! kernelTests for the fd table + open-file pool. The syscall handlers
//! are thin wrappers over the `fd*` functions tested here directly (with
//! an explicit pid, since the tests don't run as a scheduled process).
//! File-backed fds hit the real VFS/ext2 rootfs; scratch files are left
//! behind (the image is rebuilt each `zig build`).
const abi = @import("abi");
const kernel_test = @import("kernel_test");
const main = @import("main.zig");

const serial_if = main.serial_if;

const BACKING_TTY: u8 = 1;
const BACKING_FILE: u8 = 2;

fn openScratch(t: *kernel_test.Tracker, pid: u32, path: [*:0]const u8) u32 {
    const fd = main.fdOpenat(pid, path, abi.O_CREAT | abi.O_RDWR, 0o644);
    t.expectTrue(@src(), fd >= 3);
    return if (fd >= 0) @intCast(fd) else 0;
}

fn eqBytes(t: *kernel_test.Tracker, got: []const u8, want: []const u8) void {
    t.expectEqual(@src(), got.len, want.len);
    var ok = true;
    for (got, 0..) |c, i| {
        if (i < want.len and c != want[i]) ok = false;
    }
    t.expectTrue(@src(), ok);
}

// --- 1. default table -> stdin/out/err on the console ------

fn testDefaultTable() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.openDefaults(777), 0);
    t.expectEqual(@src(), main.openDefaults(777), abi.EEXIST);

    t.expectEqual(@src(), main.testFdBacking(777, 0), BACKING_TTY);
    t.expectEqual(@src(), main.testFdBacking(777, 1), BACKING_TTY);
    t.expectEqual(@src(), main.testFdBacking(777, 2), BACKING_TTY);
    t.expectEqual(@src(), main.testFdBacking(777, 3), @as(u8, 0));

    t.expectEqual(@src(), main.clearTable(777), 0);
    t.expectEqual(@src(), main.clearTable(777), abi.EINVAL);
    return t.result();
}

// --- 2. openat / write / lseek / read round-trip ---------

fn testOpenatWriteRead() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.openDefaults(777), 0);

    const fd = openScratch(&t, 777, "/fd_rw");
    t.expectEqual(@src(), fd, 3); // lowest free after 0/1/2

    t.expectEqual(@src(), main.fdWrite(777, fd, "abcdef", 6), 6);
    t.expectEqual(@src(), main.fdLseek(777, fd, 0, abi.SEEK_SET), 0);

    var buf: [16]u8 = undefined;
    t.expectEqual(@src(), main.fdRead(777, fd, &buf, 6), 6);
    eqBytes(&t, buf[0..6], "abcdef");

    t.expectEqual(@src(), main.fdClose(777, fd), 0);
    t.expectEqual(@src(), main.clearTable(777), 0);
    return t.result();
}

// --- 3. lseek whences + negative rejection --------------

fn testLseekWhences() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.openDefaults(777), 0);
    const fd = openScratch(&t, 777, "/fd_seek");
    t.expectEqual(@src(), main.fdWrite(777, fd, "0123456789", 10), 10);

    var buf: [16]u8 = undefined;
    t.expectEqual(@src(), main.fdLseek(777, fd, 3, abi.SEEK_SET), 3);
    t.expectEqual(@src(), main.fdRead(777, fd, &buf, 4), 4);
    eqBytes(&t, buf[0..4], "3456");

    t.expectEqual(@src(), main.fdLseek(777, fd, -2, abi.SEEK_CUR), 5);
    t.expectEqual(@src(), main.fdRead(777, fd, &buf, 2), 2);
    eqBytes(&t, buf[0..2], "56");

    t.expectEqual(@src(), main.fdLseek(777, fd, -1, abi.SEEK_END), 9);
    t.expectEqual(@src(), main.fdRead(777, fd, &buf, 4), 1);
    eqBytes(&t, buf[0..1], "9");

    t.expectEqual(@src(), main.fdLseek(777, fd, -1, abi.SEEK_SET), -@as(i64, abi.EINVAL));

    t.expectEqual(@src(), main.fdClose(777, fd), 0);
    t.expectEqual(@src(), main.clearTable(777), 0);
    return t.result();
}

// --- 4. the offset advances across sequential writes ----

fn testOffsetAdvances() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.openDefaults(777), 0);
    const fd = openScratch(&t, 777, "/fd_adv");

    t.expectEqual(@src(), main.fdWrite(777, fd, "AAA", 3), 3);
    t.expectEqual(@src(), main.fdWrite(777, fd, "BBB", 3), 3);
    t.expectEqual(@src(), main.fdLseek(777, fd, 0, abi.SEEK_SET), 0);

    var buf: [16]u8 = undefined;
    t.expectEqual(@src(), main.fdRead(777, fd, &buf, 6), 6);
    eqBytes(&t, buf[0..6], "AAABBB");

    t.expectEqual(@src(), main.fdClose(777, fd), 0);
    t.expectEqual(@src(), main.clearTable(777), 0);
    return t.result();
}

// --- 5. close frees the fd; bad fd / pid -> EBADF -------

fn testCloseAndBadFd() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.openDefaults(777), 0);
    const fd = openScratch(&t, 777, "/fd_close");

    var buf: [4]u8 = undefined;
    t.expectEqual(@src(), main.fdClose(777, fd), 0);
    t.expectEqual(@src(), main.fdRead(777, fd, &buf, 4), -@as(i64, abi.EBADF));
    t.expectEqual(@src(), main.fdClose(777, fd), -@as(i64, abi.EBADF));
    t.expectEqual(@src(), main.fdRead(777, 99, &buf, 4), -@as(i64, abi.EBADF));
    t.expectEqual(@src(), main.fdRead(999_999, 0, &buf, 4), -@as(i64, abi.EBADF));

    t.expectEqual(@src(), main.clearTable(777), 0);
    return t.result();
}

// --- 6. dup shares the open-file (and its offset) -------

fn testDupSharesOffset() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.openDefaults(777), 0);
    const fd = openScratch(&t, 777, "/fd_dup");
    t.expectEqual(@src(), main.fdWrite(777, fd, "1234567890", 10), 10);
    t.expectEqual(@src(), main.fdLseek(777, fd, 0, abi.SEEK_SET), 0);

    const d = main.fdDup(777, fd);
    t.expectEqual(@src(), d, 4);

    var buf: [16]u8 = undefined;
    t.expectEqual(@src(), main.fdRead(777, fd, &buf, 5), 5);
    eqBytes(&t, buf[0..5], "12345");
    // fd 4 continues from the shared offset.
    t.expectEqual(@src(), main.fdRead(777, @intCast(d), &buf, 5), 5);
    eqBytes(&t, buf[0..5], "67890");

    t.expectEqual(@src(), main.fdClose(777, fd), 0);
    t.expectEqual(@src(), main.fdClose(777, @intCast(d)), 0);
    t.expectEqual(@src(), main.clearTable(777), 0);
    return t.result();
}

// --- 7. lseek on the console is ESPIPE -----------------

fn testLseekTtyEspipe() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.openDefaults(777), 0);
    t.expectEqual(@src(), main.fdLseek(777, 1, 0, abi.SEEK_SET), -@as(i64, abi.ESPIPE));
    t.expectEqual(@src(), main.clearTable(777), 0);
    return t.result();
}

// --- 8. writing fd 1 reaches the console --------------

fn testWriteStdout() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    t.expectEqual(@src(), main.openDefaults(777), 0);
    const msg = "[fd] stdout write ok\n";
    t.expectEqual(@src(), main.fdWrite(777, 1, msg, msg.len), @as(i64, msg.len));
    t.expectEqual(@src(), main.clearTable(777), 0);
    return t.result();
}

// --- 9. fork_table shares the open files --------------

fn testForkTableShares() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.testOpenFileCount();

    t.expectEqual(@src(), main.openDefaults(100), 0);
    const fd = openScratch(&t, 100, "/fd_fork");
    t.expectEqual(@src(), main.fdWrite(100, fd, "hello", 5), 5);

    t.expectEqual(@src(), main.forkTable(100, 200), 0);
    t.expectEqual(@src(), main.testFdBacking(200, 1), BACKING_TTY);
    t.expectEqual(@src(), main.testFdBacking(200, fd), BACKING_FILE);

    t.expectEqual(@src(), main.fdLseek(100, fd, 0, abi.SEEK_SET), 0);
    var buf: [8]u8 = undefined;
    t.expectEqual(@src(), main.fdRead(100, fd, &buf, 2), 2);
    eqBytes(&t, buf[0..2], "he");
    // child's fd shares the same open-file -> continues from offset 2.
    t.expectEqual(@src(), main.fdRead(200, fd, &buf, 3), 3);
    eqBytes(&t, buf[0..3], "llo");

    t.expectEqual(@src(), main.clearTable(100), 0);
    t.expectEqual(@src(), main.clearTable(200), 0);
    t.expectEqual(@src(), main.testOpenFileCount(), base); // nothing leaked
    return t.result();
}

// --- 10. clear_table closes every fd -----------------

fn testClearClosesAll() callconv(.c) i32 {
    var t = kernel_test.Tracker{ .serial = serial_if };
    const base = main.testOpenFileCount();

    t.expectEqual(@src(), main.openDefaults(50), 0);
    _ = openScratch(&t, 50, "/fd_c1");
    _ = openScratch(&t, 50, "/fd_c2");
    t.expectEqual(@src(), main.testOpenFileCount(), base + 3); // 1 tty + 2 files

    t.expectEqual(@src(), main.clearTable(50), 0);
    t.expectEqual(@src(), main.testOpenFileCount(), base);
    return t.result();
}

comptime {
    abi.kernelTest("default_table", &testDefaultTable);
    abi.kernelTest("openat_write_read", &testOpenatWriteRead);
    abi.kernelTest("lseek_whences", &testLseekWhences);
    abi.kernelTest("offset_advances", &testOffsetAdvances);
    abi.kernelTest("close_and_bad_fd", &testCloseAndBadFd);
    abi.kernelTest("dup_shares_offset", &testDupSharesOffset);
    abi.kernelTest("lseek_tty_espipe", &testLseekTtyEspipe);
    abi.kernelTest("write_stdout", &testWriteStdout);
    abi.kernelTest("fork_table_shares", &testForkTableShares);
    abi.kernelTest("clear_closes_all", &testClearClosesAll);
}
