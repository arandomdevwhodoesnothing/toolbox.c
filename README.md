# toolbox.c

gnu coreutils reimplantation. Written in C from Scratch. Note: Some commands may not work or not implemented
You can find pre-made linux binaries at linux-binaries repository 

Compress the tar.iz4 file to install toolbox 

## Tools Supported
  
# File & Directory Operations
- **basename** - Extract the final component from a file path
- **dirname**  - Retrieve the directory path from a full file path
- **cp** - Duplicate files and directories with extensive options (-a, -b, -d, -f, -i, -l, -L, -n, -P, -p, -R, -s, -S, -t, -T, -u, -v, -x, --strip-trailing-slashes)
- **rm** - Delete files or directories with safety options (-f, -i, -I, -r/-R, -d, -v, --preserve-root, --no-preserve-root, --one-file-system)
- **mkdir** - Create new directories (-m, -p, -v)
- **rmdir** - Remove empty directories (-p, -v, --ignore-fail-on-non-empty)
- **touch** - Update file timestamps or create empty files (-a, -c, -d, -f, -m, -r, -t, --time)
- **chmod** - Modify file permissions (-c, -f, -v, -R, --no-preserve-root, --preserve-root, --reference)
- **chown** - Change file ownership (-c, -f, -v, -h, -L, -R, --preserve-root, --no-preserve-root, --reference)

# Navigation & Information
- **pwd** - Display current working directory path (-L, -P)
- **ls** - List directory contents with rich formatting (-a, -A, -b, -B, -c, -d, -f, -F, -g, -G, -h, -i, -l, -L, -n, -o, -p, -q, -Q, -r, -R, -s, -S, -t, -u, -U, -v, -x, -X, -1, --color)
- **df** - Report filesystem disk space usage (-a, -B, --block-size, -h, -H, -i, -k, -l, -m, --no-sync, --output, -P, --sync, --total, -t, --type, -T, -x, --exclude-type)
- **du** - Estimate file and directory space usage (-a, --apparent-size, -b, --bytes, -B, --block-size, -c, --total, -d, --max-depth, -D, --dereference-args, -h, -H, --inodes, -k, -l, --count-links, -L, -m, -P, -s, -S, -t, --threshold, -x, -X, --exclude, -0, --null)

# File Movement & Linking
- **mv** - Move or rename files/directories (-f, --force, -i, --interactive, -n, --no-clobber, -v, --verbose, -u, --update, -b, --backup, -S=SUFFIX, -t=DIR, -T, --strip-trailing-slashes, -h, --help)
- **ln** - Create hard or symbolic links (-b, --backup, -f, --force, -i, --interactive, -L, --logical, -n, --no-dereference, -P, --physical, -r, --relative, -s, --symbolic, -S, --suffix, -t, --target-directory, -T, --no-target-directory, -v, --verbose)

# Text Processing
- **cat** - Concatenate and display files (-A, -b, -e, -E, -n, -s, -t, -T, -u, -v)
- **tac** - Display files in reverse order (-b, -r (not implemented), -s)
- **head** - Output the beginning of files (-c, -n, -q, -v, -V, -h)
- **tail** - Output the end of files (-c, -f, -F, -n, -q, -v, -z, -s, --pid, --retry, --max-unchanged-stats)
- **cut** - Extract sections from each line of input (-b, -c, -d, -f, --complement, -s, --only-delimited, --output-delimiter)
- **sort** - Sort lines of text files (-b, -d, -f, -g, -h, -i, -M, -n, -R, -r, -V, -c, -C, -k, -m, -o, -s, -S, -t, -T, -u, -z, --parallel)
- **uniq** - Report or omit repeated lines (-c, -d, -D, -f, -i, -s, -u, -w, -z)
- **wc** - Count lines, words, and bytes (-c, -m, -l, -w, -L)
- **tee** - Read from stdin and write to stdout and files (-a, -i)

# Advanced Text Manipulation
- **awk** - Pattern scanning and processing language (-v, -f, -F, -fs)
- **sed** - Stream editor for filtering and transforming text (-n, -e, -f, -i, -E/-r) with commands: s, d, p, q, Q, a, i, c, y, =, l, r, R, w, n, N, P, D, h, H, g, G, x, :, b, t, T, { }

# System Information
- **date** - Display or set system date/time (-d, -f, -I[TIMESPEC], -R, --rfc-3339, -r, -s, -u, --utc, --universal)
- **uname** - Print system kernel information (-a, -s, -n, -r, -v, -m, -p, -i, -o)
- **whoami** - Display current username
- **find** - Search for files in directory hierarchy (-name, -iname, -type, -size, -mtime, -maxdepth, -mindepth, -empty, -print, -delete, -exec, -not/!, -and/-a, -or/-o, ())

# Utilities
- **sleep** - Delay execution for specified duration
- **yes** - Output repeated string until killed (defaults to 'y')
- **true** - Return successful exit status
- **false** - Return unsuccessful exit status
