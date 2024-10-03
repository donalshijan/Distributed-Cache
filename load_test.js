import http from 'k6/http';
import { check, sleep } from 'k6';
import { Trend, Rate } from 'k6/metrics';

// Configuration
export const options = {
    vus: 50, // Virtual Users
    duration: '60s', // Test duration
    thresholds: {
        http_req_duration: ['p(95)<500'], // 95% of requests must complete below 500ms
        'successful_requests': ['rate>0.95'], // 95% of requests must be successful
    },
};

// Define metrics
const requestDuration = new Trend('request_duration');
const successfulRequests = new Rate('successful_requests');

const cacheServerIp = __ENV.CACHE_SERVER_IP || '127.0.0.1';
const cacheServerPort = __ENV.CACHE_SERVER_PORT || 6379;

// Function to create a SET request
function constructSetMessage(key, value) {
    return `*3\r\n$3\r\nSET\r\n${`$${key.length}\r\n${key}\r\n`}${`$${value.length}\r\n${value}\r\n`}`;
}

// Function to create a GET request
function constructGetMessage(key) {
    return `*2\r\n$3\r\nGET\r\n${`$${key.length}\r\n${key}\r\n`}`;
}

// Main function to execute the test
export default function () {
    // Generate random key-value pairs
    const key = `key${Math.floor(Math.random() * 1000)}`; // Random key
    const value = Math.random().toString(36).substring(7); // Random value

    // Perform SET operation
    const setMessage = constructSetMessage(key, value);
    const setUrl = `http://${cacheServerIp}:${cacheServerPort}`;
    const setResponse = http.post(setUrl, setMessage);

    // Check the response of the SET operation
    const setSuccess = check(setResponse, {
        'set status is 200': (r) => r.status === 200,
        'set response is OK': (r) => r.body.includes('+OK'),
    });

    if (setSuccess) {
        successfulRequests.add(1);
    }

    // Measure duration for the SET request
    requestDuration.add(setResponse.timings.duration);

    // Sleep to simulate think time
    sleep(1); // Pause for 1 second before the next request

    // Perform GET operation
    const getMessage = constructGetMessage(key);
    const getResponse = http.post(setUrl, getMessage);

    // Check the response of the GET operation
    const getSuccess = check(getResponse, {
        'get status is 200': (r) => r.status === 200,
        'get response is valid': (r) => r.body.startsWith('$'),
    });

    if (getSuccess) {
        successfulRequests.add(1);
    }

    // Measure duration for the GET request
    requestDuration.add(getResponse.timings.duration);
}

