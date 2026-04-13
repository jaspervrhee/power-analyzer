"""
IEM3250 Modbus RTU Connection Module
Handles serial connection to Schneider IEM3250 Energy Meter
"""

from pymodbus.client import ModbusSerialClient


class IEM3250Connection:
    """Connection handler for IEM3250 Energy Meter over Modbus RTU"""
    
    def __init__(self, port='/dev/serial0', baudrate=19200, device_id=1, timeout=2):
        """
        Initialize connection parameters
        
        Args:
            port: Serial port (default: /dev/serial0)
            baudrate: Baud rate (default: 19200)
            device_id: Modbus device ID / slave address (default: 1)
            timeout: Response timeout in seconds (default: 2)
        """
        self.port = port
        self.baudrate = baudrate
        self.device_id = device_id
        self.timeout = timeout
        self.client = None
    
    def connect(self):
        """
        Connect to the IEM3250 device
        
        Returns:
            bool: True if connection successful, False otherwise
        """
        self.client = ModbusSerialClient(
            port=self.port,
            baudrate=self.baudrate,
            parity='E',
            stopbits=1,
            bytesize=8,
            timeout=self.timeout,
        )
        
        if not self.client.connect():
            return False
        return True
    
    def disconnect(self):
        """Close the connection"""
        if self.client:
            self.client.close()
            self.client = None
    
    def is_connected(self):
        """Check if connection is active"""
        return self.client is not None
    
    def read_registers(self, address, count):
        """
        Read holding registers from device
        IEM3250 uses an offset of -2 in Modbus addresses
        
        Args:
            address: Starting register address
            count: Number of registers to read
            
        Returns:
            list: Register values, or None if error
        """
        if not self.is_connected():
            raise RuntimeError("Not connected. Call connect() first.")
        
        # Apply -2 offset for IEM3250 Modbus addresses
        modbus_address = address - 2
        
        try:
            result = self.client.read_holding_registers(
                modbus_address,
                count=count,
                device_id=self.device_id
            )
            
            if result.isError():
                return None
            
            return result.registers
        except Exception:
            return None
    
    def __enter__(self):
        """Context manager entry"""
        self.connect()
        return self
    
    def __exit__(self, *args):
        """Context manager exit"""
        self.disconnect()
