# FileSystems
Emulate a file system using FUSE and provids the implementations for various file-related syscalls.

## Status

[X] getattr() method works entirely.

[X] mkdir() method works entirely.

[X] readdir() method works entirely.

[X] mknod() method works entirely.

[-] write() method works for first node only--POSSIBLY for multiple nodes,
        but this cannot be determined because of read() not being complete.

[-] read() method works for first node only--did not have the time to
        properly debug this method so cannot tell for sure what the issue
        is with files that span multiple nodes.

NOTE: rmdir(), unlink(), truncate(), open(), and flush() methods are
        untouched as they were not part of the scope of this project.
