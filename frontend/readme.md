## ▶️ Running the System Locally

To run the complete distributed system for development and testing, you will need **4 separate terminal windows**. The startup order is important.

### **Terminal 1: Start the Front-End**

The Front-End is the stable entry point for clients. It must be started first.

* **Command:**
    ```bash
    ./bin/frontend_exec 127.0.0.1 4000
    ```
* **Description:**
    * `127.0.0.1`: The IP address the Front-End will bind to for its listeners.
    * `127.0.0.1`: The IP address of the *initial* primary server it should connect to.
    * `4000`: The service port of the initial primary server.
    * *The Front-End will start listening for clients on port `8080` and for leader notifications on port `9090`.*

### **Terminal 2: Start the Primary Server**

The primary server needs to know its own IP and the Front-End's IP for notifications.

* **Command:**
    ```bash
    ./bin/server_exec -p --ip 127.0.0.1 --frontend-ip 127.0.0.1
    ```
* **Description:**
    * `-p`: Starts the server in **Primary** mode.
    * `--ip 127.0.0.1`: Sets its own network identity.
    * `--frontend-ip 127.0.0.1`: Tells the server where the Front-End is located.

### **Terminal 3: Start the Backup Server**

The backup server needs to know its own IP, the primary's IP, and the Front-End's IP.

* **Command:**
    ```bash
    ./bin/server_exec -b 127.0.0.1 --ip 127.0.0.2 --frontend-ip 127.0.0.1
    ```
* **Description:**
    * `-b 127.0.0.1`: Starts the server in **Backup** mode, monitoring the primary at `127.0.0.1`.
    * `--ip 127.0.0.2`: Sets its own unique network identity.
    * `--frontend-ip 127.0.0.1`: Tells the server where the Front-End is located.
    * *(You can start more backups in new terminals, using unique IPs like `--ip 127.0.0.3`)*

### **Terminal 4: Start the Client**

The client connects to the **Front-End's public port (8080)**.

* **Command:**
    ```bash
    ./bin/myClient test_user 127.0.0.1 8080
    ```
* **Description:**
    * `test_user`: The username for the session.
    * `127.0.0.1`: The IP address of the **Front-End**.
    * `8080`: The public listening port of the **Front-End**.

---

## 🧪 Testing Fault Tolerance

With all services running, you can simulate a primary server crash to test the leader election and recovery.

1.  **Find the Primary Server's Process ID (PID):**
    ```bash
    pgrep -f "./bin/server_exec -p"
    ```

2.  **Simulate the crash by killing the process:**
    *(Replace `<PRIMARY_PID>` with the number returned by the previous command)*
    ```bash
    kill -9 <PRIMARY_PID>
    ```

After this, observe the logs in the other terminals. The backup server should get promoted, the Front-End should be notified, and the client should be able to continue working seamlessly after a brief recovery period.