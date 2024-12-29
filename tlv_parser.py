#!/usr/bin/env python3

import os
import sys
from enum import IntEnum
from tkinter import Tk, Button, Label, Text, Scrollbar, Frame, BOTH, END, Entry
from tabulate import tabulate

# Set up paths from environment variables
CHIP_HOME = os.getenv("CHIP_HOME")

if not CHIP_HOME:
    print("Error: Please set the CHIP_HOME environment variable.")
    sys.exit(1)

# Update sys.path for TLVReader import based on CHIP_HOME
sys.path.insert(0, os.path.join(CHIP_HOME, 'src/controller/python'))
try:
    from chip.tlv import TLVReader
except ImportError:
    print("chip.tlv module not found. Ensure CHIP_HOME is set correctly.")
    sys.exit(1)

class TAG(IntEnum):
    TIMESTAMP = 0
    LABEL = 1,
    VALUE = 2,

data_list = []

def parse_data(file_path):
    global data_list
    data_list = []
    try:
        with open(file_path, 'rb') as file:
            binary_data = file.read()

        binary_data = bytes([0x17]) + binary_data + bytes([0x18])

        t = TLVReader(binary_data)
        data = t.get()
        for tag, entry in data['Any']:
            data_list.append(entry)

    except FileNotFoundError:
        output_text.insert(END, f"Error: File '{file_path}' not found.\n")
    except ValueError as e:
        output_text.insert(END, f"Error parsing TLV data: {e}\n")

def display_data():
    output_text.delete(1.0, END)
    if data_list:
        output_text.insert(END, f"\n{'*' * 25} Diagnostic Data {'*' * 25}\n", "center")
        
        # Create a table of diagnostic data
        table_data = [
            {
                "Timestamp": f"{entry.get(TAG.TIMESTAMP, 0):.0f}",
                "Label": entry.get(TAG.LABEL, ""),
                "Value": entry.get(TAG.VALUE, "")
            }
            for entry in data_list
        ]
        
        headers = ["Timestamp", "Label", "Value"]
        formatted_data = [list(row.values()) for row in table_data]
        output_text.insert(END, tabulate(formatted_data, headers=headers, tablefmt="fancy_grid"), "center")
        output_text.insert(END, f"\n{'*' * 70}\n", "center")
    else:
        output_text.insert(END, "No diagnostic data available.\n", "center")

def convert_data():
    file_path = file_path_entry.get().strip()
    parse_data(file_path)
    display_data()

# GUI Setup
root = Tk()
root.title("TLV Diagnostic Data Parser")
root.geometry("800x600")
root.configure(bg="#282C34")

title_label = Label(root, text="TLV Diagnostic Data Parser", font=("Arial", 18, "bold"), fg="#61AFEF", bg="#282C34")
title_label.pack(pady=10)

file_path_label = Label(root, text="Enter Binary File Path:", font=("Arial", 12), fg="white", bg="#282C34")
file_path_label.pack(pady=5)

file_path_entry = Entry(root, font=("Arial", 12), width=60)
file_path_entry.pack(pady=5)

convert_button = Button(root, text="Parse", command=convert_data, font=("Arial", 12), bg="#98C379", fg="white", width=15)
convert_button.pack(pady=10)

output_frame = Frame(root)
output_frame.pack(pady=10, fill=BOTH, expand=True)

scrollbar = Scrollbar(output_frame)
scrollbar.pack(side="right", fill="y")

output_text = Text(output_frame, wrap="word", yscrollcommand=scrollbar.set, font=("Courier New", 10), bg="#000000", fg="white", padx=10, pady=10)
output_text.tag_configure("center", justify="center")
output_text.pack(fill=BOTH, expand=True)

scrollbar.config(command=output_text.yview)

root.mainloop()
