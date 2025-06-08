#!/bin/sh
# Script to reapply patches after FreeBSD updates
# Save to /usr/local/bin/repatch_st.sh and make executable:
#   chmod +x /usr/local/bin/repatch_st.sh

# Configuration
PORT_NAME="st"                  # Port to patch (e.g., x11/st)
PATCH_FILE="/path/to/your/patch.diff"  # Path to your custom patch
BACKUP_DIR="/usr/local/etc/patches"    # Where to backup original files

# Create backup dir if missing
mkdir -p "$BACKUP_DIR"

# Navigate to the port directory
cd "/usr/ports/x11/${PORT_NAME}" || exit 1

# Step 1: Backup original files before patching
echo "[+] Backing up original source files..."
make extract
cp -r "$(make -V WRKSRC)" "${BACKUP_DIR}/${PORT_NAME}-original"

# Step 2: Apply your patch
echo "[+] Applying patch: ${PATCH_FILE}..."
cd "$(make -V WRKSRC)" || exit 1
if ! patch -p1 < "$PATCH_FILE"; then
    echo "[-] Patch failed! Restoring original files..."
    cp -r "${BACKUP_DIR}/${PORT_NAME}-original"/* .
    exit 1
fi

# Step 3: Rebuild and reinstall
echo "[+] Rebuilding port with patch..."
cd "/usr/ports/x11/${PORT_NAME}" || exit 1
make clean install

echo "[+] Done! ${PORT_NAME} has been patched."
