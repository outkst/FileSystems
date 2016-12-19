# FileSystems
Emulate a file system using FUSE and provide the implementations for various file-related syscalls.

## Status

- [X] _getattr()_ method works entirely.

- [X] _mkdir()_ method works entirely.

- [X] _readdir()_ method works entirely.

- [X] _mknod()_ method works entirely.

- [ ] _write()_ method works for first node only—POSSIBLY for multiple nodes,
    but this cannot be determined because of _read()_ not being complete.

- [ ] _read()_ method works for first node only—did not have the time to 
    properly debug this method so cannot tell for sure what the issue 
    is with files that span multiple nodes.

**NOTE:** _rmdir()_, _unlink()_, _truncate()_, _open()_, and _flush()_ methods are
        untouched as they were not part of the scope of this project.
