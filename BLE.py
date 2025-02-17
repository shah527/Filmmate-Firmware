import asyncio
import random
from bleak import BleakClient, BleakScanner

TARGET_NAME = "FilmMate Tripod"  # Device name to look for

def notification_handler(sender, data):
    # Process incoming notification data and print it
    value = int.from_bytes(data, byteorder="little", signed=True)
    print(f"Received notification: {value}")

async def main():
    # Scanning for the target device
    print("Scanning for device...")
    devices = await BleakScanner.discover()
    target = None
    for d in devices:
        if d.name and TARGET_NAME in d.name:
            target = d
            break
    if not target:
        print("Device not found.")
        return
    print(f"Found device: {target.name} ({target.address})")
    
    # Connecting to the device and discovering its services
    async with BleakClient(target.address) as client:
        print("Connected!")
        await asyncio.sleep(5)  # Allow time for service discovery
        services = client.services
        print("Discovered services and characteristics:")
        target_char = None
        # Searching for a characteristic that supports write and notify
        for service in services:
            print(f"Service: {service.uuid}")
            for char in service.characteristics:
                print(f"  Characteristic: {char.uuid}, Properties: {char.properties}")
                if "write" in char.properties and "notify" in char.properties:
                    target_char = char
                    break
            if target_char:
                break

        if not target_char:
            print("No suitable characteristic found!")
            return
        
        print(f"Using characteristic: {target_char.uuid}")
        await client.start_notify(target_char.uuid, notification_handler)  # Start notifications

        # Sending random data continuously
        try:
            while True:
                num = random.randint(0, 100)
                data = num.to_bytes(4, byteorder="little", signed=True)
                print(f"Sending: {num}")
                await client.write_gatt_char(target_char.uuid, data)
                await asyncio.sleep(5)
        except KeyboardInterrupt:
            print("Exiting...")
        finally:
            await client.stop_notify(target_char.uuid)  # Stop notifications

if __name__ == "__main__":
    asyncio.run(main())
