import time
import socket
import json
import mysql.connector
from datetime import datetime
from coapthon.client.helperclient import HelperClient

DONGLE_IPS = [
    "fd00::f6ce:366c:f0fd:f7e5",
    "fd00::f6ce:3636:5325:98f8"
]
COAP_PORT = 5683
COAP_PATH = "status"  
POLL_INTERVAL_SECONDS = 10


DB_CONFIG = {
    'host': 'localhost',
    'user': 'root',               
    'password': 'PASSWORD',       
    'database': 'smart_lamps_db' 
}

# --- FUNCTIONS ---

def setup_database():
    """
    Connects to MySQL, creates the database and tables if they don't exists.
    At every start, it flushes the tables.
    """
    db_connection = None
    try:
        db_connection = mysql.connector.connect(
            host=DB_CONFIG['host'],
            user=DB_CONFIG['user'],
            password=DB_CONFIG['password']
        )
        cursor = db_connection.cursor()
        
        db_name = DB_CONFIG['database']
        cursor.execute(f"CREATE DATABASE IF NOT EXISTS {db_name}")
        print(f"Database '{db_name}' created.")
        cursor.execute(f"USE {db_name}")

        cursor.execute("""
            CREATE TABLE IF NOT EXISTS dongles (
                id INT AUTO_INCREMENT PRIMARY KEY,
                ipv6_address VARCHAR(45) UNIQUE NOT NULL
            )
        """)

        cursor.execute("""
            CREATE TABLE IF NOT EXISTS readings (
                id INT AUTO_INCREMENT PRIMARY KEY,
                dongle_id INT NOT NULL,
                timestamp DATETIME NOT NULL,
                lux_perceived INT,
                lux_desired INT,
                brightness_percent INT,
                power_consumption_watt DECIMAL(6, 3),
                FOREIGN KEY (dongle_id) REFERENCES dongles(id) ON DELETE CASCADE
            )
        """)
        print("'dongles' and 'readings' tables created/verified.")

        print("Flushing the tables")
        cursor.execute("SET FOREIGN_KEY_CHECKS = 0")
        cursor.execute("TRUNCATE TABLE readings")
        cursor.execute("TRUNCATE TABLE dongles")
        cursor.execute("SET FOREIGN_KEY_CHECKS = 1")
        db_connection.commit()
        print("Tables flushed")

        dongle_map = {}
        for ip in DONGLE_IPS:
            cursor.execute("INSERT INTO dongles (ipv6_address) VALUES (%s)", (ip,))
        db_connection.commit()
        
        cursor.execute("SELECT id, ipv6_address FROM dongles WHERE ipv6_address IN (%s, %s)", tuple(DONGLE_IPS))
        for dongle_id, ip_addr in cursor.fetchall():
            dongle_map[ip_addr] = dongle_id
        
        print(f"Dongles registered in the database: {dongle_map}")
        
        cursor.close()
        return db_connection, dongle_map

    except mysql.connector.Error as err:
        print(f"Fatal error during database setup: {err}")
        if db_connection:
            db_connection.close()
        exit(1) 

def fetch_data_from_dongle(ip_address):
    """
    Executes a CoAP GET request to one dongle and returns the informations.
    """
    client = None
    try:
        print(f"[{datetime.now().strftime('%H:%M:%S')}] Coap Request to://[{ip_address}]:{COAP_PORT}/{COAP_PATH}")
        client = HelperClient(server=(ip_address, COAP_PORT))
        response = client.get(COAP_PATH, timeout=5)

        if response and response.payload:
            data = json.loads(response.payload)
            print(f"  -> response from {ip_address}: {data}")
            return data
        else:
            print(f"  -> Error: no response or empty payload from {ip_address}")
            return None
    except socket.timeout:
        print(f"  -> Error: Timeout from {ip_address}")
        return None
    except json.JSONDecodeError:
        print(f"  -> Error: Unable to decode the JSON payload. Payload: {response.payload}")
        return None
    except Exception as e:
        print(f"  -> Error in communicating with: {ip_address}: {e}")
        return None
    finally:
        if client:
            client.stop()

def main():

    db_connection, dongle_map = setup_database()

    try:
        while True:
            print("\n--- New sampling cycle ---")
            for ip_address, dongle_id in dongle_map.items():
                
                data = fetch_data_from_dongle(ip_address)

                if data:
                    try:

                        lux_p = data['lux_perceived']
                        lux_d = data['lux_desired']
                        brightness = data['brightness_percent']
                        
                        power = 0.5 + (brightness / 100.0 * 8.5)

                        cursor = db_connection.cursor()
                        sql = """
                            INSERT INTO readings 
                            (dongle_id, timestamp, lux_perceived, lux_desired, brightness_percent, power_consumption_watt) 
                            VALUES (%s, %s, %s, %s, %s, %s)
                        """
                        val = (dongle_id, datetime.now(), lux_p, lux_d, brightness, power)
                        cursor.execute(sql, val)
                        db_connection.commit()
                        print(f"  -> Data from {ip_address} saved in database.")
                        cursor.close()
                    
                    except (KeyError, TypeError) as e:
                        print(f"  -> Error: Data from  {ip_address} non valid. Details: {e}")

            print(f"\n--- Cycle completed. waiting for next one in {POLL_INTERVAL_SECONDS} seconds... ---")
            time.sleep(POLL_INTERVAL_SECONDS)

    except KeyboardInterrupt:
        print("\nUser closed application.")
    finally:
        if db_connection and db_connection.is_connected():
            db_connection.close()
            print("Closed database connection.")

if __name__ == "__main__":
    main()
