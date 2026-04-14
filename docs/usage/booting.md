# Booting BoredOS

BoredOS uses the Limine bootloader, which provides a flexible way to configure the boot process and pass parameters to the kernel.

## Boot Parameters

You can modify system behavior at startup by passing specific boot flags.

### Verbose Boot (`-v`)

The `-v` flag enables the kernel console (`kconsole`) during the boot process. When enabled, the kernel will display detailed initialization logs on the screen. By default, this is often disabled in the included configuration for a cleaner "splash-only" boot experience.

#### Toggling Verbose Boot at Runtime

You can enable or disable the verbose boot log directly from the Limine boot menu without modifying the source files:

1.  **Select Entry**: When the Limine boot menu appears, highlight the **BoredOS** entry.
2.  **Edit**: Press `E` to enter the entry editor.
3.  **Modify Flag**: Find the line containing `cmdline: -v`. 
    -   To **Enable**: Remove the `#` character if the line is commented out (change `# cmdline: -v` to `cmdline: -v`).
    -   To **Disable**: Add a `# ` at the start of the line.
4.  **Boot**: Press `F10` to boot using the modified parameters.

#### Persistent Configuration

To change the default behavior permanently, modify the `limine.conf` file in the repository root before building the ISO:

```conf
/BoredOS
    protocol: limine
    path: boot():/boredos.elf
    cmdline: -v
```
