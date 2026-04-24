#!/bin/bash

# ============================================================================
# Obscurity Test File Creator v1.0
# Creates test files of specified size with different content patterns.
# ============================================================================

# Banner
clear
echo "=========================================="
echo "  Obscurity Test File Creator v1.0"
echo "  Generate test files for encryption"
echo "=========================================="
echo ""

# Function to display menu
show_menu() {
    echo "What would you like to do?"
    echo "  1) Create a new test file"
    echo "  2) Exit"
    echo ""
    read -p "Enter choice [1-2]: " choice
    case $choice in
        1) create_file ;;
        2) echo "Goodbye!"; exit 0 ;;
        *) echo "Invalid choice. Please try again."; show_menu ;;
    esac
}

# Function to create a test file
create_file() {
    local filename
    local size_mb
    local content_type
    local source

    echo ""
    read -p "Enter output filename (default: testfile.bin): " filename
    filename=${filename:-testfile.bin}

    while true; do
        read -p "Enter file size in megabytes (positive integer): " size_mb
        if [[ "$size_mb" =~ ^[0-9]+$ ]] && [ "$size_mb" -gt 0 ]; then
            break
        else
            echo "Invalid size. Please enter a positive integer."
        fi
    done

    echo ""
    echo "Select content type:"
    echo "  1) All zeros (\\x00)"
    echo "  2) All ones (\\xff)"
    echo "  3) Random data (using /dev/urandom)"
    read -p "Choice [1-3]: " content_type

    case $content_type in
        1) source="zero" ;;
        2) source="one" ;;
        3) source="random" ;;
        *) echo "Invalid choice. Defaulting to random."; source="random" ;;
    esac

    echo ""
    echo "Creating $filename of size ${size_mb} MB with $source content..."

    # Create the file using dd
    if [ "$source" = "zero" ]; then
        dd if=/dev/zero of="$filename" bs=1M count="$size_mb" status=progress
    elif [ "$source" = "one" ]; then
        dd if=/dev/zero bs=1M count="$size_mb" | tr '\000' '\377' > "$filename"
        echo "Writing all ones..."
        for ((i=1; i<=size_mb; i++)); do
            printf "\rProgress: %d / %d MB" $i $size_mb
            dd if=/dev/zero bs=1M count=1 2>/dev/null | tr '\000' '\377' >> "$filename"
        done
        echo ""
    else
        dd if=/dev/urandom of="$filename" bs=1M count="$size_mb" status=progress
    fi

    if [ $? -eq 0 ]; then
        echo "File created successfully: $filename"
        # Show file size and hash (optional)
        ls -lh "$filename"
        echo "SHA256: $(sha256sum "$filename" | cut -d' ' -f1)"
    else
        echo "Error creating file."
    fi

    echo ""
    read -p "Press Enter to return to menu..."
}

# Main loop
while true; do
    show_menu
done
