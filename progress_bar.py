import sys
import time

class ProgressBar:
    def __init__(self, total):
        self.total = total
        self.current = 0
        self.running = True

    def update(self, count):
        self.current = count

    def display(self):
        while self.running:
            percent = (self.current / self.total) * 100
            bar_length = 40  # Length of the progress bar
            filled_length = int(bar_length * percent // 100)
            bar = '█' * filled_length + '-' * (bar_length - filled_length)
            sys.stdout.write(f'\rProgress: |{bar}| {percent:.2f}% Complete')
            sys.stdout.flush()
            time.sleep(0.1)  # Update every 100 ms
        print()  # Move to the next line after completion

    def stop(self):
        self.running = False
