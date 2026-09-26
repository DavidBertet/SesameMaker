# How to Add a New WebSocket Endpoint (SesameMaker)

This guide explains how to add a new WebSocket endpoint between the client and server, including backend and frontend changes, mocking, and consumption.

---

## 1. Backend: Add a New WebSocket Endpoint

### a. Create the Handler Function

- **File:** `backend/src/app/ws_<endpoint>.c` and `backend/src/app/ws_<endpoint>.h`
  (`backend/src/core/` for shared endpoints)
- **Function signature:**
  ```c
  void ws_handle_<endpoint>(const cJSON *root, int sockfd);
  ```
  - Parse the incoming JSON (`root`).
  - Respond using:
    - `send_message_sockfd(json, sockfd);` to target a single user.
    - `broadcast_message(json);` to send to all connected clients.

### b. Expose the Handler

- **File:** `backend/src/app/ws_<endpoint>.h` (`backend/src/core/` for shared endpoints)
- **Action:** Declare the handler:
  ```c
  void ws_handle_<endpoint>(const cJSON *root, int sockfd);
  ```

### c. Register the Callback

- **File:** `backend/src/app/main.c`
- **Action:**
  - Include "ws\_<endpoint>.h"
  - In `app_main()`, register your handler:
  ```c
  register_callback("<endpoint>", ws_handle_<endpoint>);
  ```

---

## 2. Backend: Sending Messages

- **Target a single user:**  
  Use `send_message_sockfd(json, sockfd);`
- **Broadcast to all users:**  
  Use `broadcast_message(json);`
- **Target by session token (from HTTP request):**  
  Use `send_message_token(json, token);`
  Retrieve the token from HTTP request using `get_session_from_cookies`

---

## 3. Frontend: Mock the Endpoint for the sample

- **File:** `frontend/src/lib/mockdata.js`
- **Action:** Add a case to `generateMockResponse(data)`:
  ```js
  case '<endpoint>':
    return [
      {
        type: '<response_type>',
        // mock data
      }
    ]
  ```

---

## 4. Frontend: Call and Consume the Endpoint

### a. Send a Message

- **File:** Any Svelte component or JS module
- **Action:**
  ```js
  import { sendMessage } from 'src/core/lib/ws.svelte.js'
  sendMessage({ type: '<endpoint>', ...payload })
  ```

### b. Listen for Responses

- **File:** Any Svelte component or JS module
- **Action:**
  ```js
  import { onMessageType } from 'src/core/lib/ws.svelte.js'
  const unsubscribe = onMessageType('<response_type>', (data) => {
    // handle response
  })
  ```

### c. Example Usage in Svelte

```svelte
<script>
  import { sendMessage, onMessageType } from 'src/core/lib/ws.svelte.js';
  let response = null;
  let unsub = $state(null)

  onMount(() => {
    const unsub = onMessageType('<response_type>', (data) => {
      response = data;
    });
    sendMessage({ type: '<endpoint>', ...payload });
    return () => {
      if (unsub) unsub()
    }
  });
</script>

<!-- use response -->
```
