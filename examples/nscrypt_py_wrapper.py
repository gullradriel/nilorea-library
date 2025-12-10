#!/usr/bin/env python

from ctypes import *
from sys import platform
import sys

is_64bits = sys.maxsize > 2**32

if is_64bits:
    print('Python is running as 64-bit application')
else:
    print('Python is running as 32-bit application')

#! wrapper to N_STR
class N_STR(Structure):
    _fields_ = [ ('data', c_char_p) , ('length', c_size_t) , ('written', c_size_t) ]

print( "OS: ",platform )

if platform == "linux" or platform == "linux2":
    api = cdll.LoadLibrary("./libnilorea.so")
elif platform == "win32":
    api = cdll.LoadLibrary("./libnilorea64.dll")
elif platform == "win64":
    api = cdll.LoadLibrary("./libnilorea64.dll")

api.char_to_nstr.argtype = c_char_p
api.char_to_nstr.restype = c_void_p

if platform == "linux" or platform == "linux2":
    input_file = N_STR.from_address( api.char_to_nstr( b"ex_common" ) );   
elif platform == "win32" and is_64bits :
    input_file = N_STR.from_address( api.char_to_nstr( b"ex_crypto32.exe" ) );   
elif platform == "win64" and not is_64bits:
    input_file = N_STR.from_address( api.char_to_nstr( b"ex_crypto64.exe" ) );   

example_text= N_STR.from_address( api.char_to_nstr( b"##############################################################\n# This is an example of crypto encode/decode, with a newline #\n# This is the end of the test                                #\n##############################################################" ) );
print( 'example text ',example_text.written,'/',example_text.length,": ",example_text.data )

api.n_vigenere_get_rootkey.argtype = c_uint
api.n_vigenere_get_rootkey.restype = c_void_p
#rootkey = N_STR.from_address( api.n_vigenere_get_rootkey( 128 ))
rootkey = N_STR.from_address( api.char_to_nstr( b"RKCHSLZWFNASULJFVPRVUUUAEUVEMSSGEVNWIMVPZVIWITBGIUBEQVEURBYEWTMCQZZYNWQMRAIBTMHDIKIZHOVZQUFONRQDSRDFNTTGVEKOSTSAEABLOXMGTTWIMPNE" ) )
print( "root key: ",rootkey.written,"/",rootkey.length,": ",rootkey.data )

api.n_vigenere_get_question.argtype = c_int
api.n_vigenere_get_question.restype = c_void_p
question = N_STR.from_address( api.n_vigenere_get_question( 128 ))
print( "question: ",question.written,"/",question.length,": ",question.data )

api.n_vigenere_get_answer.argtype = [c_void_p,c_void_p]
api.n_vigenere_get_answer.restype = c_void_p
answer = N_STR.from_address( api.n_vigenere_get_answer( byref( rootkey ) , byref( question ) ) )
print( "answer ",answer.written,"/",answer.length,": ",answer.data )


api.n_base64_encode.argtype = c_void_p
api.n_base64_encode.restype = c_void_p
base64_data = N_STR.from_address( api.n_base64_encode( byref( example_text ) ) )
print( "base64 encoded ",base64_data.written,"/",base64_data.length,": ",base64_data.data )

api.n_vigenere_encode.argtype = [c_void_p,c_void_p]
api.n_vigenere_encode.restype = c_void_p
encoded = N_STR.from_address( api.n_vigenere_encode( byref( base64_data ) , byref( rootkey ) ) )
print( "encoded with rootkey ",encoded.written,"/",encoded.length,": ",encoded.data )

api.n_vigenere_decode.argtype = [c_void_p,c_void_p]
api.n_vigenere_decode.restype = c_void_p
decoded = N_STR.from_address( api.n_vigenere_decode( byref( encoded ) , byref( rootkey ) ) )

api.n_base64_decode.argtype = c_void_p
api.n_base64_decode.restype = c_void_p
base64_decoded = N_STR.from_address( api.n_base64_decode( byref( decoded ) ) )
print( "decoded with rootkey ",base64_decoded.written,"/",base64_decoded.length,": ",base64_decoded.data )

api.n_vigenere_decode_qa.argtype = [ c_void_p , c_void_p , c_void_p ]
api.n_vigenere_decode_qa.restype = c_void_p 
decoded_qa = N_STR.from_address( api.n_vigenere_decode_qa( byref( encoded ) , byref( question ) , byref( answer ) ) ) 
print( "decoded with question and answer ",decoded_qa.written,"/",decoded_qa.length,": ",decoded_qa.data )


# Files
output_file_encoded_root = N_STR.from_address( api.char_to_nstr( b"ex_crypto64.encoded_root" ) );   
output_file_decoded_root = N_STR.from_address( api.char_to_nstr( b"ex_crypto64.decoded_root" ) );   
output_file_encoded_qa = N_STR.from_address( api.char_to_nstr( b"ex_crypto64.encoded_qa" ) );   
output_file_decoded_qa = N_STR.from_address( api.char_to_nstr( b"ex_crypto64.decoded_qa" ) );   
output_file_qa_decoded_qa = N_STR.from_address( api.char_to_nstr( b"ex_crypto64.qa_decoded_qa" ) );   
output_file_qa_decoded_root = N_STR.from_address( api.char_to_nstr( b"ex_crypto64.qa_decoded_root" ) );   

api.n_vigenere_encode_file.argtype = [ c_void_p , c_void_p , c_void_p ]
api.n_vigenere_encode_file.restype = c_int 
result=api.n_vigenere_encode_file( byref( input_file ) , byref( output_file_encoded_root ) , byref( rootkey ) )  
print( "encoded file with root: ",result,": ",output_file_encoded_root.data )

api.n_vigenere_decode_file.argtype = [ c_void_p , c_void_p , c_void_p ]
api.n_vigenere_decode_file.restype = c_int 
result=api.n_vigenere_decode_file( byref( output_file_encoded_root ) , byref( output_file_decoded_root ) , byref( rootkey ) )  
print( "decoded file with root: ",result,": ",output_file_decoded_root.data )

api.n_vigenere_decode_file_qa.argtype = [ c_void_p , c_void_p , c_void_p , c_void_p ]
api.n_vigenere_decode_file_qa.restype = c_int 
result=api.n_vigenere_decode_file_qa( byref( output_file_encoded_root ) , byref( output_file_decoded_qa ) , byref( question ) , byref( answer ) )
print( "decoded file with question & answer: ",result,": ",output_file_decoded_qa.data )

api.n_vigenere_encode_file_qa.argtype = [ c_void_p , c_void_p , c_void_p , c_void_p ]
api.n_vigenere_encode_file_qa.restype = c_int 
result=api.n_vigenere_encode_file_qa( byref( input_file ) , byref( output_file_encoded_qa ) , byref( question ) , byref( answer ) )
print( "encoded file with question & answer: ",result,": ",output_file_decoded_qa.data )

api.n_vigenere_decode_file_qa.argtype = [ c_void_p , c_void_p , c_void_p , c_void_p ]
api.n_vigenere_decode_file_qa.restype = c_int 
result=api.n_vigenere_decode_file_qa( byref( output_file_encoded_qa ) , byref( output_file_qa_decoded_qa ) , byref( question ) , byref( answer ) )
print( "decoded file qa with question & answer: ",result,": ",output_file_qa_decoded_qa.data )

api.n_vigenere_decode_file.argtype = [ c_void_p , c_void_p , c_void_p ]
api.n_vigenere_decode_file.restype = c_int 
result=api.n_vigenere_decode_file( byref( output_file_encoded_qa ) , byref( output_file_qa_decoded_root ) , byref( rootkey ) )  
print( "decoded file qa with root: ",result,": ",output_file_qa_decoded_root.data )

