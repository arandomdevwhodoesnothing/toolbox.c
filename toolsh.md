# toolsh

A lightweight Linux shell for toolbox written in C.

---

### Pipes
```sh
echo -e "banana\napple\ncherry" | sort
cat file.txt | head -20 | wc -l
ls | sort -r | tail -5
```

### Redirection
```sh
echo hello > out.txt
echo world >> out.txt
cat < out.txt
ls 2> errors.txt
```

### Variables
```sh
export NAME=world
echo Hello $NAME
echo "PID is $$, last exit: $?"
echo ${HOME}/documents
```

### Conditionals
```sh
true && echo "runs"
false || echo "fallback"
test -f file.txt && cat file.txt || echo "not found"
```

### Semicolons
```sh
echo one; echo two; echo three
mkdir mydir; cd mydir; echo hello > file.txt
```

### Background jobs
```sh
sleep 10 &        # [1] 1234
jobs              # [1] Running   sleep 10
```

### Aliases
```sh
alias ll="ls -la"
alias ..="cd .."
alias       # list all
unalias ll
```

Pre-loaded defaults: `ll`, `la`, `l`, `..`, `...`

### History
- 500-entry ring buffer
- `!!` — repeat last command
- `!3` — repeat 3rd history entry
- `history` — show all entries

### Interactive line editing
| Key | Action |
|-----|--------|
| `↑` / `↓` | Navigate history |
| `←` / `→` | Move cursor |
| `Home` / `End` | Jump to line start/end |
| `Ctrl-A` / `Ctrl-E` | Same as Home/End |
| `Ctrl-U` | Clear line |
| `Ctrl-L` | Clear screen |
| `Ctrl-D` | Exit shell |
| `Backspace` / `Delete` | Delete character |

### Built-in commands
| Command | Description |
|---------|-------------|
| `cd [dir]` | Change directory (`cd -` goes back) |
| `echo [-n\|-e]` | Print text with escape support |
| `export VAR=val` | Export variable to environment |
| `set [VAR=val]` | Set variable or list all |
| `unset VAR` | Remove variable |
| `alias [n=val]` | Create or list aliases |
| `unalias name` | Remove alias |
| `history` | Show command history |
| `jobs` | List background jobs |
| `source` / `.` | Run script in current shell |
| `type cmd` | Show how a command resolves |
| `tools` | List all available tools |
| `help` | Show this reference |
| `pwd` | Print working directory |
| `true` / `false` | Return exit codes 0 / 1 |
| `clear` | Clear the terminal |
| `exit [code]` | Exit the shell |

### Script mode
```sh
toolsh script.sh          # run a script file
toolsh -c 'echo hello'    # run a single command
```

### Startup file
On launch, toolsh sources `~/.toolshrc` if it exists. Use it for aliases, exports, and any startup commands.