import logging
import azure.functions as func
import io
import pandas as pd
import matplotlib.pyplot as plt
import smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
from email.mime.base import MIMEBase
from email import encoders
from azure.storage.blob import BlobServiceClient
 
app = func.FunctionApp()
# CRON of "0 0 8 * * 5" means that the code will be executed every Friday at 8 am
@app.schedule(schedule="0 0 8 * * 5", arg_name="myTimer", run_on_startup=True,
              use_monitor=False)
def timer_trigger(myTimer: func.TimerRequest) -> None:
    if myTimer.past_due:
        logging.info("The timer is past due!")
 
    logging.info("Python timer trigger function executed.")
 
    # Azure Blob Storage configuration
    connection_string = "" # ENTER CONNECTION STRING
    container_name = "" # ENTER CONTAINER NAME
    blob_name = "" # ENTER BLOB NAME
 
    # Retrieve data from Blob Storage
    blob_service_client = BlobServiceClient.from_connection_string(connection_string)
    blob_client = blob_service_client.get_blob_client(container_name, blob_name)
    csv_data = blob_client.download_blob().content_as_text()
 
    # Use StringIO to read CSV string
    csv_buffer = io.StringIO(csv_data)
    df = pd.read_csv(csv_buffer)
    df['time'] = pd.to_datetime(df['time'])
 
    # Create DataFrame and plot
    plt.figure(figsize=(10, 6))
    plt.plot(df['time'], df['soundLevel'], label='Sound Level')
    plt.plot(df['time'], df['lightIntensity'], label='Light Intensity')
    plt.xlabel('Time')
    plt.ylabel('Measurement')
    plt.title('Sound Level and Light Intensity Over Time')
    plt.legend()
    graph_filename = "/tmp/graph.png"  # Save in current directory
    plt.savefig(graph_filename)
 
    # Email configuration
    smtp_server = ""  # Your SMTP server
    smtp_port = 587
    email_user = ""  # Your email address
    email_password = ""  # Your email password
 
    sender_email = email_user
    receiver_email = "" # Caretaker email
    subject = "Graph from Azure Function"
    body = "Please find the attached graph."
 
    # Create a multipart email
    message = MIMEMultipart()
    message["From"] = sender_email
    message["To"] = receiver_email
    message["Subject"] = subject
 
    # Attach the body
    message.attach(MIMEText(body, "plain"))
 
    # Attach the graph
    with open(graph_filename, "rb") as attachment:
        part = MIMEBase("application", "octet-stream")
        part.set_payload(attachment.read())
        encoders.encode_base64(part)
        part.add_header("Content-Disposition", "attachment; filename=graph.png")
        message.attach(part)
 
    # Send the email
    with smtplib.SMTP(smtp_server, smtp_port) as server:
        server.starttls()  # Start TLS for security
        server.login(email_user, email_password)  # Login with your email and password
        server.sendmail(sender_email, receiver_email, message.as_string())
 