import os
import sys
import requests
from bs4 import BeautifulSoup
from urllib.parse import urljoin

def download_image(image_url, save_dir):
    try:
        response = requests.get(image_url, stream=True)
        response.raise_for_status()
        
        # Ottieni il nome del file dall'URL
        filename = os.path.join(save_dir, os.path.basename(image_url))
        
        with open(filename, 'wb') as file:
            for chunk in response.iter_content(chunk_size=8192):
                file.write(chunk)
                
        print(f"Image saved: {filename}")
    except requests.exceptions.RequestException as e:
        print(f"Failed to download {image_url}: {e}")

def download_images_from_html(file_path, base_url, save_dir):
    if not os.path.exists(save_dir):
        os.makedirs(save_dir)

    with open(file_path, 'r', encoding='utf-8') as file:
        soup = BeautifulSoup(file, 'html.parser')
        images = soup.find_all('img')
        
        for img in images:
            src = img.get('src')
            if src:
                # Gestisce URL relativi
                image_url = urljoin(base_url, src)
                download_image(image_url, save_dir)

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: python download_images.py <html_file_path> <base_url> <save_directory>")
        sys.exit(1)

    html_file_path = sys.argv[1]  # Percorso del file HTML locale
    base_url = sys.argv[2]  # URL base del sito web per gestire URL relativi
    save_directory = sys.argv[3]  # Directory dove salvare le immagini

    download_images_from_html(html_file_path, base_url, save_directory)
