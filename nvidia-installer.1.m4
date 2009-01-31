dnl This file is to be preprocessed by m4.
changequote([[[, ]]])dnl
define(__OPTIONS__, [[[include([[[options.1.inc]]])dnl]]])dnl
.\" Copyright (C) 2005-2009 NVIDIA Corporation.
.\"
__HEADER__
.\" Define the URL macro and then load the URL package if it exists.
.de URL
\\$2 \(laURL: \\$1 \(ra\\$3
..
.if \n[.g] .mso www.tmac
.TH nvidia\-installer 1 2009-01-09 "nvidia\-installer __VERSION__"
.SH NAME
nvidia\-installer \- install, upgrade, or uninstall the NVIDIA Accelerated Graphics Driver Set
.SH SYNOPSIS
.B nvidia\-installer
[
.I options
]
.SH DESCRIPTION
.PP
.B nvidia\-installer
is a tool for installing, updating, and uninstalling the NVIDIA __INSTALLER_OS__ Graphics Driver.
When the driver is installed by running, for example:
.sp
.ti +5
sh NVIDIA\-__INSTALLER_OS__\-__INSTALLER_ARCH__\-__DRIVER_VERSION__\-pkg1.run
.sp
The .run file unpacks itself and invokes the contained
.B nvidia\-installer
utility.
.B nvidia\-installer
then walks you through the installation process.
As part of installation,
.B nvidia\-installer
is installed on the system for later use, such as for uninstalling the currently installed driver or updating to newer drivers.
.PP
In the default 'install' mode of operation,
.B nvidia\-installer
probes your system to determine which driver files should be installed and where.
.B nvidia\-installer
then determines which files already present on your system will conflict with driver installation; those files are noted and will be later backed up.
At this point, nothing will have been changed on the file system, though a "command list" will have been generated.
If running in expert mode (requested with the
.B \-\-expert
commandline option), this command list will be presented so that you can review all operations that
.B nvidia\-installer
intends to perform.
Finally,
.B nvidia\-installer
executes the commandlist, backing up conflicting files already present on the file system, installing the new driver files on the file system, creating symlinks, and running system utilities like
.BR depmod (8)
and
.BR ldconfig (8).
.PP
To later uninstall the NVIDIA __INSTALLER_OS__ graphics driver, you can run
.B nvidia\-installer \-\-uninstall.
In the 'uninstall' mode of operation, all driver files that were installed on the system are deleted, and all files that were backed up during installation are restored to their original locations.
The uninstall process should restore your filesystem to its state prior to installation.
If you install one NVIDIA __INSTALLER_OS__ graphics driver while another is already installed, this uninstall step is automatically performed on the old driver at the beginning of installation of the new driver.
.PP
You can also use
.B nvidia\-installer
to automatically update to newer drivers.
.PP
You can query the latest driver available on NVIDIA's website with the
.B \-\-latest
option, or request that the latest driver, if newer than your current driver, be automatically downloaded and installed by specifying the
.B \-\-update
commandline option.
.PP
\fBnvidia\-installer\fR's backend is separate from its user interface; the installer will use an ncurses-based user interface if it can find the correct ncurses library, otherwise, it will fall back to a simple commandline user interface.
To disable use of the ncurses user interface, use the option
.B \-\-ui=none.
Additional user interfaces, utilizing GTK+ or QT, for example, could be provided in the future.
.\" XXX should we describe precompiled kernel interfaces here?
.PP
The source code to
.B nvidia\-installer
is released under the GPL and available here:
.sp
.ti +5
.URL "ftp://download.nvidia.com/XFree86/nvidia\-installer/"
.sp
Patches are welcome.
dnl Call gen-manpage-opts to generate this section.
__OPTIONS__
.SH "DISTRIBUTION HOOK SCRIPTS"
.PP
Because the NVIDIA installer may interact badly with distribution packages that contain the NVIDIA driver,
.B nvidia\-installer
provides a mechanism for the distribution to handle manual installation of the driver.
If they exist,
.B nvidia\-installer
will run the following scripts:
.RS
\(bu /usr/lib/nvidia/pre\-install
.br
\(bu /usr/lib/nvidia/pre\-uninstall
.br
\(bu /usr/lib/nvidia/post\-uninstall
.br
\(bu /usr/lib/nvidia/post\-install
.br
\(bu /usr/lib/nvidia/failed\-install
.RE
Note that if installation of a new driver requires uninstallation of a previously installed driver, the
.B pre\-
and
.B post\-uninstall
scripts will be called
.I after
the
.B pre\-install
script.
If the install fails, the installer will execute
.B /usr/lib/nvidia/failed\-install
instead of
.BR /usr/lib/nvidia/post\-install .
These scripts should not require user interaction.
.PP
Use the
.B \-\-no\-distro\-scripts
option to disable execution of these scripts.
.SH EXAMPLES
.TP
.B nvidia\-installer \-\-latest
Connect to NVIDIA's FTP site, and report the latest driver version and the URL to the latest driver file.
.TP
.B nvidia\-installer \-\-update
Connect to NVIDIA's FTP site.
If a newer version of the driver is available, download and install it.
Use
.B \-\-force\-update
to install the most recent driver even if
.B \-\-nvidia\-installer
detects that it is installed already.
.TP
.B nvidia\-installer \-\-uninstall
Remove the NVIDIA driver and restore files that were overwritten during the install process.
.\" .SH FILES
.\" .I /usr/lib/libGL.so.NNNN
.SH AUTHOR
Aaron Plattner
.br
NVIDIA Corporation
.SH "SEE ALSO"
.BR nvidia-xconfig (1),
.BR nvidia-settings (1),
.I /usr/share/doc/NVIDIA_GLX-1.0/README.txt
.SH COPYRIGHT
Copyright \(co 2005-2009 NVIDIA Corporation.
