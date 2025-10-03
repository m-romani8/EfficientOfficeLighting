import mysql.connector
from coapthon.client.helperclient import HelperClient

DB_CONFIG = {
    'host': 'localhost',
    'user': 'root',
    'password': 'PASSWORD',
    'database': 'smart_lamps_db'
}

COAP_PORT = 5683

COAP_ACTUATOR_PATH = "actuators/brightness"

# --- Support Functions ---

def print_help():
    """Print available commands list"""
    print("\n--- Smart Lamp User Application ---")
    print("Available commands:")
    print("  last5      - Display the last 5 sensor readings from all dongles.")
    print("  lastHour   - Calculate and display the total energy consumption in the last hour.")
    print("  allOff     - Send a command to turn off all lamps.")
    print("  allOn      - Send a command to turn on all lamps to 20% brightness.")
    print("  help       - Show this list of commands.")
    print("  exit / quit- Close the application.")
    print("-" * 35)

def get_dongle_ips(cursor):
    """Retrieves all IPv6 addresses available."""
    try:
        cursor.execute("SELECT ipv6_address FROM dongles")
        results = [item[0] for item in cursor.fetchall()]
        return results
    except mysql.connector.Error as err:
        print(f"Error fetching dongle IPs from database: {err}")
        return []

def send_brightness_command(ip_address, brightness_percent):
    """
    Send PUT CoAP request to set brightness.
    """
    client = None
    try:
        payload = str(brightness_percent)
        
        print(f"Sending command to {ip_address}: set brightness to {brightness_percent}% (payload: '{payload}')")
        
        client = HelperClient(server=(ip_address, COAP_PORT))

        response = client.put(COAP_ACTUATOR_PATH, payload, timeout=5)
        
        if response:
            print(f"  -> Success! Dongle responded with code: {response.code}")
        else:
            print("  -> Failure! No response from dongle (timeout or error).")
            
    except Exception as e:
        print(f"  -> An error occurred while sending command to {ip_address}: {e}")
    finally:
        if client:
            client.stop()

# --- Commands Handler ---

def handle_last5(cursor):
    """Print last 5 records from 'readings'."""
    try:

        query = """
            SELECT d.ipv6_address, r.timestamp, r.lux_perceived, r.lux_desired, 
                   r.brightness_percent, r.power_consumption_watt
            FROM readings r
            JOIN dongles d ON r.dongle_id = d.id
            ORDER BY r.timestamp DESC
            LIMIT 5
        """
        cursor.execute(query)
        results = cursor.fetchall()
        
        if not results:
            print("No records found in the 'readings' table.")
            return
            
        print("\n--- Last 5 Readings ---")

        print(f"{'IPv6 Address':<40} {'Timestamp':<20} {'Lux (P)':<10} {'Lux (D)':<10} {'Bright(%)':<10} {'Power(W)':<10}")
        print("-" * 110)

        for row in results:
            ipv6, ts, lux_p, lux_d, bright, power = row
            print(f"{ipv6:<40} {str(ts):<20} {lux_p:<10} {lux_d:<10} {bright:<10} {power:<10.2f}")
            
    except mysql.connector.Error as err:
        print(f"Database error while fetching last 5 records: {err}")

def handle_last_hour(cursor):
    """Computes and prints the average and total power consumption of the last hour."""
    try:

        query = """
            SELECT AVG(power_consumption_watt) 
            FROM readings 
            WHERE timestamp >= NOW() - INTERVAL 1 HOUR
        """
        cursor.execute(query)

        result = cursor.fetchone()[0]
        
        if result is None:
            print("No data recorded in the last hour to calculate consumption.")
            return
        
        avg_watts = float(result)

        total_joules = avg_watts * 3600
        
        print("\n--- Energy Consumption in the Last Hour ---")
        print(f"Average power consumption: {avg_watts:.2f} Wh")
        print(f"Total energy consumed (estimated): {total_joules / 1000:.2f} kJ (kiloJoules)")

    except mysql.connector.Error as err:
        print(f"Database error while calculating consumption: {err}")

# --- Main Function ---

def main():
    db_connection = None
    try:

        db_connection = mysql.connector.connect(**DB_CONFIG)
        print("Successfully connected to the database.")

        print_help()
        
        while True:

            command = input("\nEnter command > ").strip().lower()
            
            if command in ("exit", "quit"):
                print("Exiting application.")
                break
            
            cursor = db_connection.cursor()
            
            if command == "last5":
                handle_last5(cursor)
            elif command == "lasthour":
                handle_last_hour(cursor)
            elif command == "alloff":
                dongle_ips = get_dongle_ips(cursor)
                for ip in dongle_ips:
                    send_brightness_command(ip, 0)
            elif command == "allon":
                dongle_ips = get_dongle_ips(cursor)
                for ip in dongle_ips:
                    send_brightness_command(ip, 20)
            elif command == "help":
                print_help()
            else:
                print(f"Unknown command: '{command}'. Type 'help' to see available commands.")
            
            cursor.close()

    except mysql.connector.Error as err:
        print(f"\nFATAL: Could not connect to database. Please check DB_CONFIG and ensure MySQL is running.")
        print(f"Error details: {err}")
    finally:
        if db_connection and db_connection.is_connected():
            db_connection.close()
            print("\nDatabase connection closed.")

if __name__ == "__main__":
    main()
