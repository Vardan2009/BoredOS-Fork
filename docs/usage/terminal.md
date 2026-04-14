# Terminal & Command Line

The BoredOS Terminal provides a powerful command-line interface (CLI) for advanced users and developers. It supports standard Unix-like features and provides direct access to the kernel's system calls.

## The Shell

The default shell in BoredOS is a custom-built, lightweight command processor integrated into the Window Manager. It features:
-   **ANSI Color Support**: Rich text output with colors and styles.
-   **Command History**: Use the **Up** and **Down** arrow keys to navigate through your previous commands (up to 64 history entries).
-   **Output Redirection**:
    -   `command > file`: Write output to a new file (or overwrite existing).
    -   `command >> file`: Append output to an existing file.
-   **Piping**:
    -   `command1 | command2`: Pass the output of the first command as input to the second.

## Common Commands

Below are some of the most used commands available in `/bin`:

| Command | Description |
| :--- | :--- |
| `ls` | List files and directories in the current path. |
| `cd` | Change the current working directory. |
| `cat` | Display the contents of a file. |
| `ls` | List directory contents. |
| `rm` | Remove a file. |
| `mkdir` | Create a new directory. |
| `man` | View the manual for a specific command (e.g., `man ls`). |
| `sysfetch` | Display system and hardware information. |


---
[Return to Documentation Index](../README.md)
