import struct

# Create a simple 10x10 grid of int16 values
data = []
for i in range(10):
    for j in range(10):
        # Simple pattern: value = i * 10 + j
        value = i * 10 + j
        data.append(value)

# Write as binary int16 values
with open('sample.bil', 'wb') as f:
    for value in data:
        f.write(struct.pack('<h', value))  # Little-endian int16

print("Created sample.bil with 100 int16 values")
