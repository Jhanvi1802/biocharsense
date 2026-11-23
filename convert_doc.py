import pypandoc
import os

src = 'BiocharSense_Hardware_Review.md'
dst = 'BiocharSense_Hardware_Review.docx'

pypandoc.convert_file(src, 'docx', outputfile=dst, extra_args=['--toc', '--toc-depth=2'])
size = os.path.getsize(dst)
print(f'Wrote {dst} ({size:,} bytes)')
