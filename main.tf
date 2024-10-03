# Define the variables
variable "project_id" {
  description = "GCP Project ID"
  type        = string
}

variable "region" {
  description = "GCP Region"
  type        = string
  default     = "us-central1"  # Optional default value
}

variable "credentials_path" {
  description = "Path to the service account JSON file"
  type        = string
}

variable "service_account_email" {
  description = "Service account email"
  type        = string
}

# Use the variables in the provider block
provider "google" {
  project     = var.project_id
  region      = var.region
  credentials = file(var.credentials_path)
}

# Define a GCP Network
resource "google_compute_network" "default" {
  name = "jenkins-network"
}

# Define a firewall rule for SSH and Jenkins (port 8080)
resource "google_compute_firewall" "allow-ssh-jenkins" {
  name    = "allow-ssh-jenkins"
  network = google_compute_network.default.name

  allow {
    protocol = "tcp"
    ports    = ["22", "8080"]
  }

  source_ranges = ["0.0.0.0/0"]
}

# Define a Jenkins VM instance
resource "google_compute_instance" "jenkins" {
  name         = "jenkins-server"
  machine_type = "e2-medium"
  zone         = "us-central1-a"

  boot_disk {
    initialize_params {
      image = "ubuntu-2004-focal-v20210429"
    }
  }

  network_interface {
    network = google_compute_network.default.name
    access_config {}
  }

 metadata ={
    startup-script  = <<EOF
            #!/bin/bash
            sudo apt update
            sudo apt install -y openjdk-17-jdk docker.io
            sudo systemctl start docker
            sudo systemctl enable docker
            sudo usermod -aG docker $USER
            
            sudo apt install -y wget gnupg2

            # Install Jenkins using the official method
            sudo wget -O /usr/share/keyrings/jenkins-keyring.asc \
            https://pkg.jenkins.io/debian-stable/jenkins.io-2023.key
            echo "deb [signed-by=/usr/share/keyrings/jenkins-keyring.asc]" \
            https://pkg.jenkins.io/debian-stable binary/ | sudo tee \
            /etc/apt/sources.list.d/jenkins.list > /dev/null

            sudo apt update
            sudo apt install -y jenkins
            sudo systemctl start jenkins
            sudo systemctl enable jenkins
     EOF
}
  service_account {
    email  = var.service_account_email
    scopes = ["cloud-platform"]
  }

  tags = ["jenkins"]
}

# Output the Jenkins URL
output "jenkins_url" {
  value = "http://${google_compute_instance.jenkins.network_interface.0.access_config.0.nat_ip}:8080"
}
