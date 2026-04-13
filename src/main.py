#!/usr/bin/env python3
"""
IEM3250 Energy Meter Modbus RTU Reader
Main script to read registers from Schneider IEM3250 Energy Meter
"""

import argparse
from registers import read_and_display, scan_devices, scan_registers


def main():
    parser = argparse.ArgumentParser(
        description='Read registers from Schneider IEM3250 Energy Meter'
    )
    parser.add_argument('--port', default='/dev/serial0', 
                        help='Serial port (default: /dev/serial0)')
    parser.add_argument('--id', default=1, type=int, 
                        help='Device ID (default: 1)')
    parser.add_argument('--addr', default=3020, type=int, 
                        help='Start register address (default: 3020 = Voltage)')
    parser.add_argument('--count', default=14, type=int, 
                        help='Number of registers to read (default: 14 = all voltages)')
    parser.add_argument('--scan', action='store_true', 
                        help='Scan for active devices instead')
    parser.add_argument('--scan-regs', action='store_true',
                        help='Scan for active registers (0-5000)')
    
    args = parser.parse_args()
    
    print("=" * 70)
    print("IEM3250 Schneider Energy Meter - Register Reader")
    print("=" * 70)
    print()
    
    if args.scan:
        print(f"Scanning for devices on {args.port}...\n")
        devices = scan_devices(args.port)
        
        if devices:
            print(f"\n✓ Found {len(devices)} device(s): {devices}")
        else:
            print("\n✗ No devices found")
    elif args.scan_regs:
        print(f"Scanning for active registers on {args.port}...")
        scan_registers(port=args.port, device_id=args.id)
    else:
        print(f"Port: {args.port}")
        print(f"Device ID: {args.id}")
        print(f"Address: {args.addr}")
        print(f"Count: {args.count}")
        print()
        
        read_and_display(
            port=args.port,
            device_id=args.id,
            start_addr=args.addr,
            num_registers=args.count
        )


if __name__ == '__main__':
    main()
