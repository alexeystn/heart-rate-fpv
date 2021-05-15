import cv2
import os
import numpy as np

png_path = 'png'
mcm_path = 'mcm'

font_list = [f for f in os.listdir(png_path) if f.endswith('.png')]


for font in font_list:

    print(font)

    img_filename = os.path.join(png_path, font) 
    mcm_filename = os.path.join(mcm_path, font.split(sep='.')[0]+'.mcm')

    img = cv2.imread(img_filename)

    w = 12
    h = 18
    n = 256
    endline = '\n'
    a = np.zeros((n, h*w//4, 4), dtype='uint8')

    for i in range(n):
        x0 = (i % 16) * (w + 1)
        y0 = (i // 16) * (h + 1)
        char = img[y0:y0+h, x0:x0+w]
        char4 = np.reshape(char[:, :, 0], (-1, 4))
        a[i] = char4

    with open(mcm_filename, 'w') as f:
        f.write('MAX7456')
        for i in range(n):
            for line in a[i]:
                s = ''
                for px in line:
                    if px == 255:
                        s += '10'
                    elif px == 0:
                        s += '00'
                    else:
                        s += '01'
                f.write(endline + s)
            for t in range(64 - w*h//4):
                f.write(endline + '01010101')
