#!/bin/bash

# Check if a tool is installed
check_command() {
    if ! command -v "$1" &> /dev/null; then
        echo "$1 is required but not installed. Please install it and try again."
        exit 1
    fi
}

# Download and verify a file with its SHA512 checksum
download_and_verify() {
    local url="$1"
    local dest="$2"
    local expected_sha="$3"

    if [[ -f "$dest" ]]; then
        echo "$dest already exists. Skipping download."
        return
    fi

    echo "Downloading $url ..."
    wget -q "$url" -O "$dest"

    if [[ $? -ne 0 ]]; then
        echo "Failed to download $url"
        exit 1
    fi

    echo "Verifying SHA512 checksum..."
    local actual_sha=$(sha512sum "$dest" | awk '{print $1}')

    if [[ "$actual_sha" != "$expected_sha" ]]; then
        echo "Invalid SHA512 checksum for $dest."
        echo "Expected: $expected_sha"
        echo "Got     : $actual_sha"
        exit 1
    fi
}

# Apply patches listed in the PATCHES variable
apply_patches() {
    local name="$1"
    local patches=(${2//,/ }) # Convert comma-separated list to array
    local patch_dir="$PWD/$THIRDPARTY_DIR/patches/$name"

    for patch_file in "${patches[@]}"; do
        local patch_path="$patch_dir/$patch_file"

        if [[ -f "$patch_path" ]]; then
            echo "Applying patch: $patch_path"
            # --dry-run for debug
            patch -p1 < "$patch_path"
            if [[ $? -ne 0 ]]; then
                echo "Failed to apply patch: $patch_path"
                exit 1
            fi
        else
            echo "Patch not found: $patch_path"
            exit 1
        fi
    done
}

# Ensure the script is running from the top level
if [ ! -f ./third_party.sh ]
then
	die "Error! You must call this script only in top_srcdir!"
fi

# Initial checks
check_command wget
check_command sha512sum
check_command patch

# Check the third-party files directory
THIRDPARTY_DIR="thirdparty_repo"

# Process all files in the directory
for metadata_file in "$THIRDPARTY_DIR"/*; do
    echo "Processing file $metadata_file..."

    # Load metadata from the source file
    if [[ ! -f "$metadata_file" ]]; then
        echo "File $metadata_file not found."
        continue
    fi

    source "$metadata_file"

    if [[ -z "$Name" || -z "$Version" || -z "$Patch_version" || -z "$SRCFILE" || -z "$SHA512" || -z "$URL" ]]; then
        echo "Metadata is incomplete in $metadata_file."
        continue
    fi

    # Create the source directory
    if [[ ! -d "$SOURCESDIR" ]]; then
        mkdir -p "$SOURCESDIR"
    fi

    # Download the sources if not already downloaded
    download_and_verify "$URL" "$SOURCESDIR/$SRCFILE" "$SHA512"

    # Extract the sources
    echo "Extracting archive $SRCFILE..."
    tar -xzf "$SOURCESDIR/$SRCFILE" -C "$SOURCESDIR" --strip-components=1

    if [[ $? -ne 0 ]]; then
        echo "Failed to extract $SRCFILE"
        continue
    fi

    # Suppress (delete) the archive after extraction
    echo "Deleting archive $SRCFILE..."
    rm -f "$SOURCESDIR/$SRCFILE"

    # Apply patches if defined
    if [[ ! -z "$PATCHES" ]]; then
        echo "Applying patches..."
        apply_patches "$Name" "$PATCHES"
    else
        echo "No patches to apply."
    fi

    echo "Process completed for $metadata_file."
    echo "---"
done

echo "Process completed for all files."
