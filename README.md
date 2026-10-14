# MiniVSFS: C-Based File System Image Generator

## Project Overview

MiniVSFS is a simplified, inode-based file system inspired by VSFS, implemented in C. This project focuses on creating and modifying raw binary disk images using low-level file system structures and memory management techniques.

The project consists of two programs: `mkfs_builder`, which creates a file system image based on user-defined parameters, and `mkfs_adder`, which adds a file from the working directory to the root directory of an existing MiniVSFS image and saves the updated image.

## Features

- **File System Image Creation:** Generate raw binary disk images with configurable sizes and inode counts.
- **File Addition:** Add files to the root directory of an existing file system image.
- **Inode Management:** Manage file and directory metadata using inode tables.
- **Bitmap Management:** Track allocated and free inodes and data blocks using bitmaps.
- **Superblock and Directory Management:** Maintain essential file system metadata and directory entries.
- **Checksum Calculation:** Generate checksums for superblocks, inodes, and directory entries.
- **First-Fit Allocation:** Allocate available inodes and data blocks using the first-fit policy.
- **Command-Line Interface:** Accept configuration parameters and file paths through command-line arguments.
- **Error Handling:** Handle invalid parameters and incompatible input files gracefully.

## Technologies Used

- **Programming Language:** C
- **Core Concepts:** File Systems, Inodes, Bitmaps, Binary File I/O, Pointers, Memory Management, Data Structures, and Command-Line Argument Parsing.

## Programs

### 1. `mkfs_builder`

Creates a MiniVSFS file system image using the specified image name, size, and inode count.

### 2. `mkfs_adder`

Reads an existing MiniVSFS image, adds a specified file to its root directory, and writes the modified file system to a new binary image.

## Learning Objectives

- Understand the internal organization of inode-based file systems.
- Implement file system metadata structures in C.
- Work with raw binary disk images and low-level file operations.
- Apply inode and data block allocation techniques.
- Practice command-line argument parsing and robust error handling.

## Academic Context

Developed as a lab term project for **CSE 321:Operating Systems**, focusing on file system implementation.
