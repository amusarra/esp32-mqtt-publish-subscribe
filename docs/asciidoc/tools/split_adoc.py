import os
import re
import sys

def read_adoc_file(file_path):
    with open(file_path, 'r', encoding='utf-8') as file:
        return file.read()

def clean_title(title):
    # Elimina le parti apostrofate, rimuove i caratteri non validi per un nome di file,
    # sostituisce gli spazi con underscore, e converte in minuscolo
    title = re.sub(r"\b\w*'\w+\b", '', title)  # Elimina le parti apostrofate
    title = re.sub(r'[^\w\s]', '', title).strip().replace('-', '').lower()
    title = re.sub(r'\s+', '_', title)  # Sostituisce spazi multipli con un singolo underscore
    return title

def write_chapter_file(prefix, title, content):
    valid_title = clean_title(title)
    filename = f"{prefix}_{valid_title}.adoc"
    with open(filename, 'w', encoding='utf-8') as file:
        file.write(content)
    print(f"Created file: {filename}")
    return filename

def split_into_chapters(content):
    # Divide il contenuto in base ai titoli dei capitoli e dei sotto-capitoli
    chapters = re.split(r'(^== .*$|^=== .*$)', content, flags=re.MULTILINE)
    
    # Separa i titoli dei capitoli dai loro contenuti
    chapter_pairs = [(chapters[i], chapters[i + 1]) for i in range(1, len(chapters) - 1, 2)]
    
    return chapter_pairs

def create_index_file(filenames):
    with open("index.adoc", 'w', encoding='utf-8') as file:
        file.write("= Indice\n\n")
        for filename in filenames:
            file.write(f"include::{filename}[]\n")
    print("Created file: index.adoc")

def main(file_path):
    print(f"Processing file: {file_path}")
    content = read_adoc_file(file_path)
    chapters = split_into_chapters(content)
    
    filenames = []
    chapter_index = 1
    subchapter_index = 1
    for title, chapter_content in chapters:
        # Rimuove il simbolo di titolo "== " o "=== " dal titolo
        clean_title = title.strip().lstrip('=').strip()
        if title.startswith('== '):
            # Capitolo principale
            prefix = f"{chapter_index:03d}"
            chapter_index += 1
            subchapter_index = 1  # Reset per i sotto-capitoli
        elif title.startswith('=== '):
            # Sotto-capitolo
            prefix = f"{chapter_index-1:03d}_{subchapter_index:03d}"
            subchapter_index += 1
        else:
            continue  # Questo non dovrebbe mai accadere

        print(f"Processing chapter: {clean_title}")
        filename = write_chapter_file(prefix, clean_title, title + chapter_content)
        filenames.append(filename)
    
    create_index_file(filenames)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python split_adoc.py <path_to_adoc_file>")
        sys.exit(1)
    
    input_file = sys.argv[1]
    main(input_file)
