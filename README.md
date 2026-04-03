# Interactive Backup Management System

This project was developed as part of the *Operating Systems 1* course.
It is an interactive application for managing directory backups. The program allows users to create backups, monitor changes in real time, and synchronize multiple target directories concurrently.

---

## Features

- Create backups of directories to one or multiple target locations  
- Monitor source directory using `inotify`
- Synchronize changes in real time (files, directories, symbolic links)  
- Handle multiple backup targets concurrently
- Stop selected backups without deleting existing data  
- List active backups  
- Restore backups  

---

## Usage
After starting the program, it displays available commands and waits for user input.

## Commands

#### Add backup: ```add <source path> <target path> [target_path2 ...]```

#### Stop backup: ```end <source path> <target paths>```

#### List active backups: ```list```

#### Restore backup: ```restore <source path> <target path>```

#### Exit program: ```exit```

---
## Build & Run
```bash
make
./sop-backup
```
---

## Implementation Details

- **Language:** C (POSIX)  
- **File monitoring:** `inotify`  
- **Concurrency:** multiple processes  
- **Signal handling:** `SIGINT`, `SIGTERM`  
- **Symbolic links:** supported
