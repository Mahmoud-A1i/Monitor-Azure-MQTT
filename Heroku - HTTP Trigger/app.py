from flask import Flask, request
from twilio.rest import Client

app = Flask(__name__)

# Initialize Twilio client
account_sid = '' # Twilio SID
auth_token = '' # Twilio authentication token
twilio_client = Client(account_sid, auth_token)

# Define a route to send SMS
@app.route('/send_sms', methods=['POST'])
def send_sms():
    name = request.form.get('name', 'User')  # Get name from POST request, default to 'User'
    message = twilio_client.messages.create(
        body=f"\nAttend to {name} NOW!! \nThe environment they're in is unsafe.",
        from_='', # Twilio sender number
        to='' # ENTER THE CARETAKER'S NUMBER
    )
    return f"Message sent to {message.to}", 200  # Confirm message was sent

if __name__ == '__main__':
    app.run()  # Run the Flask application
