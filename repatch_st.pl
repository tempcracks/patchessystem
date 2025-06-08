#!/usr/bin/perl
use strict;
use warnings;
use File::Copy::Recursive qw(dircopy);
use Cwd 'abs_path';

# Configuration
my $port_name = "st";                   # Port to patch (e.g., x11/st)
my $patch_file = "/path/to/your/patch.diff";  # Path to your custom patch
my $backup_dir = "/usr/local/etc/patches";    # Where to backup original files

# Create backup dir if missing
mkdir -p $backup_dir or die "[-] Failed to create backup directory: $!\n";

# Navigate to the port directory
chdir("/usr/ports/x11/$port_name") or die "[-] Failed to change to port directory: $!\n";

# Step 1: Backup original files before patching
print "[+] Backing up original source files...\n";
system("make extract") == 0 or die "[-] make extract failed: $?\n";

my $wrksrc = `make -V WRKSRC`;
chomp($wrksrc);
$wrksrc = abs_path($wrksrc);

dircopy($wrksrc, "$backup_dir/$port_name-original") or 
    die "[-] Backup failed: $!\n";

# Step 2: Apply your patch
print "[+] Applying patch: $patch_file...\n";
chdir($wrksrc) or die "[-] Failed to change to WRKSRC directory: $!\n";

if (system("patch -p1 < '$patch_file'") != 0) {
    print "[-] Patch failed! Restoring original files...\n";
    dircopy("$backup_dir/$port_name-original", $wrksrc) or 
        die "[-] Restore failed: $!\n";
    exit 1;
}

# Step 3: Rebuild and reinstall
print "[+] Rebuilding port with patch...\n";
chdir("/usr/ports/x11/$port_name") or die "[-] Failed to return to port directory: $!\n";

system("make clean install") == 0 or 
    die "[-] make clean install failed: $?\n";

print "[+] Done! $port_name has been patched.\n";
