#!/usr/bin/env python3
"""
UDP Drawing Test Script for Otto Robot
Draws various patterns on Otto's display via UDP

Usage:
    python udp_draw_test.py <otto_ip> [pattern]
    
Patterns:
    - x: Draw X shape
    - box: Draw rectangle border
    - circle: Draw circle
    - smile: Draw smiley face
    - text: Draw "HI"
    - clear: Clear screen
    - random: Random pixels
"""

import socket
import time
import sys
import math
import random

class OttoDrawing:
    def __init__(self, ip, port=12345, width=240, height=280):
        self.ip = ip
        self.port = port
        self.width = width
        self.height = height
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        print(f"🤖 Connected to Otto robot at {ip}:{port}")
        print(f"📐 Display size: {width}x{height}")
    
    def send_pixel(self, x, y, state=1):
        """Send single pixel command"""
        if 0 <= x < self.width and 0 <= y < self.height:
            packet = f"{x},{y},{state}"
            self.sock.sendto(packet.encode(), (self.ip, self.port))
            return True
        return False
    
    def clear(self):
        """Clear entire screen"""
        print("🧹 Clearing screen...")
        for y in range(0, self.height, 10):
            for x in range(0, self.width, 10):
                self.send_pixel(x, y, 0)
            time.sleep(0.01)
    
    def draw_x(self):
        """Draw X pattern"""
        print("✖️  Drawing X...")
        size = min(self.width, self.height)
        for i in range(0, size, 2):
            # Diagonal \
            x1 = i
            y1 = int(i * self.height / self.width)
            self.send_pixel(x1, y1, 1)
            
            # Diagonal /
            x2 = self.width - i - 1
            y2 = int(i * self.height / self.width)
            self.send_pixel(x2, y2, 1)
            
            time.sleep(0.001)
    
    def draw_box(self):
        """Draw rectangle border"""
        print("📦 Drawing box...")
        margin = 20
        
        # Top and bottom
        for x in range(margin, self.width - margin, 2):
            self.send_pixel(x, margin, 1)
            self.send_pixel(x, self.height - margin, 1)
            time.sleep(0.001)
        
        # Left and right
        for y in range(margin, self.height - margin, 2):
            self.send_pixel(margin, y, 1)
            self.send_pixel(self.width - margin, y, 1)
            time.sleep(0.001)
    
    def draw_circle(self):
        """Draw circle"""
        print("⭕ Drawing circle...")
        cx = self.width // 2
        cy = self.height // 2
        radius = min(self.width, self.height) // 3
        
        for angle in range(0, 360, 2):
            rad = math.radians(angle)
            x = int(cx + radius * math.cos(rad))
            y = int(cy + radius * math.sin(rad))
            self.send_pixel(x, y, 1)
            time.sleep(0.001)
    
    def draw_smile(self):
        """Draw smiley face"""
        print("😊 Drawing smiley face...")
        cx = self.width // 2
        cy = self.height // 2
        
        # Face circle
        radius = min(self.width, self.height) // 3
        for angle in range(0, 360, 3):
            rad = math.radians(angle)
            x = int(cx + radius * math.cos(rad))
            y = int(cy + radius * math.sin(rad))
            self.send_pixel(x, y, 1)
            time.sleep(0.001)
        
        # Left eye
        eye_y = cy - radius // 3
        eye_left_x = cx - radius // 3
        for i in range(-5, 6, 2):
            for j in range(-5, 6, 2):
                self.send_pixel(eye_left_x + i, eye_y + j, 1)
        
        # Right eye
        eye_right_x = cx + radius // 3
        for i in range(-5, 6, 2):
            for j in range(-5, 6, 2):
                self.send_pixel(eye_right_x + i, eye_y + j, 1)
        
        # Smile arc
        smile_y = cy + radius // 4
        for angle in range(200, 340, 3):
            rad = math.radians(angle)
            x = int(cx + (radius * 0.6) * math.cos(rad))
            y = int(smile_y + (radius * 0.3) * math.sin(rad))
            self.send_pixel(x, y, 1)
            time.sleep(0.001)
    
    def draw_text_hi(self):
        """Draw 'HI' text"""
        print("📝 Drawing 'HI'...")
        
        # Simple 5x7 bitmap for 'H'
        h_pattern = [
            [1, 0, 0, 0, 1],
            [1, 0, 0, 0, 1],
            [1, 0, 0, 0, 1],
            [1, 1, 1, 1, 1],
            [1, 0, 0, 0, 1],
            [1, 0, 0, 0, 1],
            [1, 0, 0, 0, 1],
        ]
        
        # Simple 5x7 bitmap for 'I'
        i_pattern = [
            [1, 1, 1, 1, 1],
            [0, 0, 1, 0, 0],
            [0, 0, 1, 0, 0],
            [0, 0, 1, 0, 0],
            [0, 0, 1, 0, 0],
            [0, 0, 1, 0, 0],
            [1, 1, 1, 1, 1],
        ]
        
        scale = 8
        start_x = 40
        start_y = self.height // 2 - (7 * scale) // 2
        
        # Draw 'H'
        for y, row in enumerate(h_pattern):
            for x, pixel in enumerate(row):
                if pixel:
                    for dy in range(scale):
                        for dx in range(scale):
                            self.send_pixel(start_x + x * scale + dx, 
                                          start_y + y * scale + dy, 1)
        
        # Draw 'I'
        start_x += 60
        for y, row in enumerate(i_pattern):
            for x, pixel in enumerate(row):
                if pixel:
                    for dy in range(scale):
                        for dx in range(scale):
                            self.send_pixel(start_x + x * scale + dx, 
                                          start_y + y * scale + dy, 1)
        
        time.sleep(0.1)
    
    def draw_random(self, count=500):
        """Draw random pixels"""
        print(f"🎲 Drawing {count} random pixels...")
        for _ in range(count):
            x = random.randint(0, self.width - 1)
            y = random.randint(0, self.height - 1)
            state = random.choice([0, 1])
            self.send_pixel(x, y, state)
            time.sleep(0.002)
    
    def draw_animation(self):
        """Draw bouncing ball animation"""
        print("🏀 Drawing bouncing ball animation...")
        print("⚠️  Press Ctrl+C to stop")
        
        x, y = self.width // 2, self.height // 2
        vx, vy = 5, 7
        radius = 10
        
        try:
            while True:
                # Clear previous position (draw black circle)
                for angle in range(0, 360, 30):
                    rad = math.radians(angle)
                    px = int(x + radius * math.cos(rad))
                    py = int(y + radius * math.sin(rad))
                    self.send_pixel(px, py, 0)
                
                # Update position
                x += vx
                y += vy
                
                # Bounce off walls
                if x - radius <= 0 or x + radius >= self.width:
                    vx = -vx
                if y - radius <= 0 or y + radius >= self.height:
                    vy = -vy
                
                # Draw new position (draw white circle)
                for angle in range(0, 360, 15):
                    rad = math.radians(angle)
                    px = int(x + radius * math.cos(rad))
                    py = int(y + radius * math.sin(rad))
                    self.send_pixel(px, py, 1)
                
                time.sleep(0.03)
        
        except KeyboardInterrupt:
            print("\n⏹️  Animation stopped")

def main():
    if len(sys.argv) < 2:
        print("❌ Usage: python udp_draw_test.py <otto_ip> [pattern]")
        print("\nPatterns:")
        print("  x        - Draw X shape")
        print("  box      - Draw rectangle border")
        print("  circle   - Draw circle")
        print("  smile    - Draw smiley face")
        print("  text     - Draw 'HI'")
        print("  clear    - Clear screen")
        print("  random   - Random pixels")
        print("  animate  - Bouncing ball animation")
        print("\nExample:")
        print("  python udp_draw_test.py 192.168.1.100 smile")
        sys.exit(1)
    
    ip = sys.argv[1]
    pattern = sys.argv[2] if len(sys.argv) > 2 else "x"
    
    drawer = OttoDrawing(ip)
    
    print(f"\n🎨 Drawing pattern: {pattern}")
    print("=" * 50)
    
    if pattern == "clear":
        drawer.clear()
    elif pattern == "x":
        drawer.draw_x()
    elif pattern == "box":
        drawer.draw_box()
    elif pattern == "circle":
        drawer.draw_circle()
    elif pattern == "smile":
        drawer.draw_smile()
    elif pattern == "text":
        drawer.draw_text_hi()
    elif pattern == "random":
        drawer.draw_random()
    elif pattern == "animate":
        drawer.draw_animation()
    else:
        print(f"❌ Unknown pattern: {pattern}")
        print("Available: x, box, circle, smile, text, clear, random, animate")
        sys.exit(1)
    
    print("\n✅ Done!")

if __name__ == "__main__":
    main()
