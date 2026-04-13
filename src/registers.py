"""
IEM3250 Register Reading Module
Functions to read and display registers from IEM3250 Energy Meter
"""

import struct
from iem3250 import IEM3250Connection

# IEM3250 Register Mapping (Modbus addresses to Schneider register numbers)
# Format: schneider_register: (description, unit, address)
REGISTER_MAP = {
    3000: ("I1 - Phase 1 Current", "A"),
    3002: ("I2 - Phase 2 Current", "A"),
    3004: ("I3 - Phase 3 Current", "A"),
    3010: ("Current Average", "A"),
    3020: ("Voltage L1-L2", "V"),
    3022: ("Voltage L2-L3", "V"),
    3024: ("Voltage L3-L1", "V"),
    3026: ("Voltage L-L Average", "V"),
    3028: ("Voltage L1-N", "V"),
    3030: ("Voltage L2-N", "V"),
    3032: ("Voltage L3-N", "V"),
    3036: ("Voltage L-N Average", "V"),
    3054: ("Active Power Phase 1", "kW"),
    3056: ("Active Power Phase 2", "kW"),
    3058: ("Active Power Phase 3", "kW"),
    3060: ("Total Active Power", "kW"),
    3084: ("Power Factor", ""),
    3110: ("Frequency", "Hz"),
}


def registers_to_float32(register_list, byte_order='CDAB'):
    """
    Convert pairs of 16-bit registers to Float32 values
    Each Float32 = 2 consecutive 16-bit registers
    
    Args:
        register_list: List of 16-bit register values
        byte_order: 'ABCD' (big-endian), 'BADC' (swap-bytes), 'CDAB', 'DCBA'
        
    Returns:
        list: Float32 values
    """
    float_values = []
    for i in range(0, len(register_list) - 1, 2):
        high = register_list[i]
        low = register_list[i + 1]
        
        # Different byte orders
        if byte_order == 'ABCD':  # Big-endian (standard)
            val32 = (high << 16) | low
        elif byte_order == 'BADC':  # Swap bytes (Schneider)
            val32 = ((high & 0xFF) << 24) | ((high >> 8) << 16) | ((low & 0xFF) << 8) | (low >> 8)
        elif byte_order == 'CDAB':
            val32 = (low << 16) | high
        elif byte_order == 'DCBA':
            val32 = ((low & 0xFF) << 24) | ((low >> 8) << 16) | ((high & 0xFF) << 8) | (high >> 8)
        else:
            val32 = (high << 16) | low
        
        # Convert to float
        float_val = struct.unpack('>f', struct.pack('>I', val32))[0]
        float_values.append(float_val)
    return float_values


def read_registers(port='/dev/serial0', device_id=1, start_addr=0, num_registers=10):
    """
    Read registers from IEM3250
    
    Args:
        port: Serial port
        device_id: Modbus device ID
        start_addr: Starting register address
        num_registers: Number of registers to read
        
    Returns:
        list: Register values, or None if error
    """
    with IEM3250Connection(port=port, device_id=device_id) as conn:
        registers = conn.read_registers(start_addr, num_registers)
        
        if registers is None:
            print(f"✗ Failed to read registers")
            return None
        
        return registers


def display_registers(registers, start_addr=0):
    """
    Display registers as Float32 values with labels and units
    IEM3250 uses CDAB byte-order
    
    Args:
        registers: List of 16-bit register values
        start_addr: Starting register address
    """
    if not registers:
        print("No registers to display")
        return
    
    # IEM3250 uses CDAB byte-order
    float_values = registers_to_float32(registers, byte_order='CDAB')
    
    print("\n✓ Data read from IEM3250:\n")
    print(f"{'Reg#':<6} {'Description':<30} {'Value':<12} {'Unit':<6}")
    print("-" * 60)
    
    for i, float_val in enumerate(float_values):
        reg_addr = start_addr + (i * 2)
        
        # Look up in register map
        if reg_addr in REGISTER_MAP:
            desc, unit = REGISTER_MAP[reg_addr]
            print(f"{reg_addr:<6} {desc:<30} {float_val:<12.4f} {unit:<6}")
        else:
            print(f"{reg_addr:<6} {'Unknown':<30} {float_val:<12.4f} {'?':<6}")
    
    print()


def read_and_display(port='/dev/serial0', device_id=1, start_addr=0, num_registers=10):
    """
    Read registers and display results
    
    Args:
        port: Serial port
        device_id: Modbus device ID
        start_addr: Starting register address
        num_registers: Number of registers to read
    """
    with IEM3250Connection(port=port, device_id=device_id) as conn:
        registers = conn.read_registers(start_addr, num_registers)
        
        if registers is None:
            print(f"✗ Failed to read registers at address {start_addr}")
            return False
        
        display_registers(registers, start_addr)
        return True


def scan_devices(port='/dev/serial0', max_device_id=32):
    """
    Scan for active Modbus devices
    
    Args:
        port: Serial port
        max_device_id: Maximum device ID to scan (default: 32)
        
    Returns:
        list: Active device IDs
    """
    found_devices = []
    
    for device_id in range(1, max_device_id + 1):
        with IEM3250Connection(port=port, device_id=device_id, timeout=0.5) as conn:
            try:
                registers = conn.read_registers(0, 1)
                if registers is not None:
                    found_devices.append(device_id)
                    print(f"✓ Found device ID {device_id}")
            except:
                pass
    
    return found_devices


def scan_registers(port='/dev/serial0', device_id=1, start=0, end=5000, step=50):
    """
    Scan for active registers in a range
    
    Args:
        port: Serial port
        device_id: Modbus device ID
        start: Starting register address (default: 0)
        end: Ending register address (default: 5000)
        step: Scan step size (default: 50 registers at a time)
    """
    active_registers = []
    
    print(f"\nScanning registers {start} to {end}...")
    print(f"{'Addr':<6} {'Value':<15} {'Status':<20}\n")
    
    with IEM3250Connection(port=port, device_id=device_id) as conn:
        for addr in range(start, end, step):
            try:
                # Read a chunk of registers
                registers = conn.read_registers(addr, step)
                
                if registers is None:
                    continue
                
                # Convert to float32 values
                float_values = registers_to_float32(registers, byte_order='CDAB')
                
                # Check which ones have non-nan values
                for i, val in enumerate(float_values):
                    reg_addr = addr + (i * 2)
                    
                    # Check if value is not nan and not zero (or close to zero for current)
                    if not (val != val):  # not nan
                        # Skip very small values (likely noise)
                        if abs(val) > 0.001 or reg_addr in REGISTER_MAP:
                            active_registers.append((reg_addr, val))
                            
                            label = REGISTER_MAP.get(reg_addr, ("Unknown", "?"))[0]
                            unit = REGISTER_MAP.get(reg_addr, ("Unknown", "?"))[1]
                            
                            print(f"{reg_addr:<6} {val:<15.4f} {label} ({unit})")
            except Exception as e:
                continue
    
    print(f"\n✓ Found {len(active_registers)} active registers")
    return active_registers
